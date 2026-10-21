#:package System.Management@10.0.13
#:property TargetFramework=net10.0
#:property PublishAot=false

using System.Globalization;
using System.Management;
using System.Runtime.InteropServices;
using System.Runtime.Versioning;
using System.Text.Json;
using System.Text.RegularExpressions;
using Microsoft.Win32;

if (args is ["--help"])
{
    Console.WriteLine("""
        SerialPorts: read-only Windows COM device inventory (.NET 10).
        dotnet run SerialPorts.cs
        dotnet run SerialPorts.cs -- --json
        dotnet run SerialPorts.cs -- --watch
        dotnet run SerialPorts.cs -- --watch --json
        dotnet run SerialPorts.cs -- --self-test
        Watch checks once per second. Ctrl+C stops it.
        No serial port is opened. No firmware, driver or registry setting is changed.
        """);
    return 0;
}
if (args is ["--self-test"]) return Inventory.SelfTest();
if (args.Any(argument => argument is not ("--json" or "--watch")) ||
    args.Distinct(StringComparer.Ordinal).Count() != args.Length)
{
    Console.Error.WriteLine("Invalid arguments. Use --help.");
    return 2;
}
if (!OperatingSystem.IsWindows())
{
    Console.Error.WriteLine("Device discovery requires Windows. --self-test works without hardware.");
    return 2;
}

bool json = args.Contains("--json");
bool watch = args.Contains("--watch");
var jsonOptions = new JsonSerializerOptions { PropertyNamingPolicy = JsonNamingPolicy.CamelCase };
using var cancelled = new CancellationTokenSource();
ConsoleCancelEventHandler stop = (_, eventArgs) =>
{
    eventArgs.Cancel = true;
    cancelled.Cancel();
};
Console.CancelKeyPress += stop;
try
{
    var previous = Inventory.ReadWindows();
    if (json) Console.WriteLine(JsonSerializer.Serialize(new { kind = "snapshot", devices = previous }, jsonOptions));
    else
    {
        Console.WriteLine("Windows COM devices (read-only inventory)");
        if (previous.Length == 0) Console.WriteLine("No present COM devices found.");
        foreach (var device in previous) Inventory.Print("PRESENT", device);
        Console.WriteLine("VID/PID identify a USB interface, not necessarily the board model.");
        if (watch) Console.WriteLine("Unplug/replug one USB device. Checking every second. Ctrl+C stops.");
    }
    while (watch && !cancelled.IsCancellationRequested)
    {
        await Task.Delay(TimeSpan.FromSeconds(1), cancelled.Token);
        var current = Inventory.ReadWindows();
        foreach (var change in Inventory.Changes(previous, current))
        {
            if (json) Console.WriteLine(JsonSerializer.Serialize(change, jsonOptions));
            else Inventory.Print(change.Kind.ToUpperInvariant(), change.Device);
        }
        previous = current;
    }
    return 0;
}
catch (OperationCanceledException) when (cancelled.IsCancellationRequested) { return 0; }
catch (Exception error) when (error is ManagementException or COMException or IOException or UnauthorizedAccessException or
    System.Security.SecurityException or InvalidOperationException)
{
    Console.Error.WriteLine($"Device inventory failed ({error.GetType().Name}): {error.Message}");
    return 1;
}
finally { Console.CancelKeyPress -= stop; }

sealed record ComDevice(
    string Port, string Name, string? BusDescription, string? ReportedManufacturer,
    string? Vid, string? Pid, string InstanceId, string? ParentInstanceId, string? Location);

sealed record DeviceChange(string Kind, ComDevice Device);

static class Inventory
{
    private static readonly Regex PortPattern = new(@"\((COM[0-9]+)\)$", RegexOptions.IgnoreCase);
    private static readonly Regex UsbPattern = new(@"VID_([0-9A-F]{4})[&+]PID_([0-9A-F]{4})",
        RegexOptions.IgnoreCase);

    [SupportedOSPlatform("windows")]
    public static ComDevice[] ReadWindows()
    {
        try { return ReadWindowsSnapshot(); }
        catch (Exception error) when (
            error is ManagementException { ErrorCode: ManagementStatus.NotFound } ||
            error is IOException && (error.HResult & 0xffff) == 1018)
        {
            Console.Error.WriteLine("A device disappeared during discovery. Retrying the snapshot once.");
            return ReadWindowsSnapshot();
        }
    }

    [SupportedOSPlatform("windows")]
    private static ComDevice[] ReadWindowsSnapshot()
    {
        using var searcher = new ManagementObjectSearcher(
            new ManagementScope(@"root\cimv2"),
            new ObjectQuery("SELECT * FROM Win32_PnPEntity WHERE PNPClass = 'Ports' AND Present = TRUE"),
            new System.Management.EnumerationOptions
                { Timeout = TimeSpan.FromSeconds(5), ReturnImmediately = false });
        using var results = searcher.Get();
        var devices = new List<ComDevice>();
        foreach (ManagementObject entity in results)
        {
            using (entity)
            {
                string? instanceId = entity["PNPDeviceID"] as string;
                if (string.IsNullOrWhiteSpace(instanceId))
                    throw new InvalidOperationException("A present port has no PnP instance ID.");
                string name = entity["Name"] as string ?? "";
                using var key = Registry.LocalMachine.OpenSubKey(
                    $@"SYSTEM\CurrentControlSet\Enum\{instanceId}\Device Parameters", writable: false);
                string? port = key?.GetValue("PortName") as string;
                if (string.IsNullOrWhiteSpace(port))
                {
                    Match match = PortPattern.Match(name.Trim());
                    port = match.Success ? match.Groups[1].Value : null;
                }
                if (port is null || !Regex.IsMatch(port, @"^COM[0-9]+$", RegexOptions.IgnoreCase))
                    continue; // The Ports class also contains parallel ports such as LPT1.

                var properties = ReadProperties(entity);
                var (vid, pid) = UsbIds(instanceId);
                devices.Add(new ComDevice(port.ToUpperInvariant(), name,
                    properties.GetValueOrDefault("DEVPKEY_Device_BusReportedDeviceDesc"),
                    entity["Manufacturer"] as string, vid, pid, instanceId,
                    properties.GetValueOrDefault("DEVPKEY_Device_Parent"),
                    properties.GetValueOrDefault("DEVPKEY_Device_LocationInfo")));
            }
        }
        return devices.OrderBy(device => PortNumber(device.Port)).ToArray();
    }

    [SupportedOSPlatform("windows")]
    private static Dictionary<string, string?> ReadProperties(ManagementObject entity)
    {
        using var input = entity.GetMethodParameters("GetDeviceProperties");
        input["devicePropertyKeys"] = new[]
        {
            "DEVPKEY_Device_BusReportedDeviceDesc",
            "DEVPKEY_Device_Parent",
            "DEVPKEY_Device_LocationInfo"
        };
        using var output = entity.InvokeMethod("GetDeviceProperties", input,
            new InvokeMethodOptions { Timeout = TimeSpan.FromSeconds(5) });
        if (output is null || Convert.ToUInt32(output["ReturnValue"], CultureInfo.InvariantCulture) != 0)
            throw new InvalidOperationException("Windows did not return the requested device properties.");
        var values = new Dictionary<string, string?>(StringComparer.Ordinal);
        if (output["deviceProperties"] is ManagementBaseObject[] properties)
        {
            foreach (var property in properties)
            {
                using (property)
                {
                    string? key = property["KeyName"] as string;
                    if (key is null) continue;
                    var data = property.Properties.Cast<PropertyData>().FirstOrDefault(item => item.Name == "Data");
                    values[key] = data?.Value as string;
                }
            }
        }
        return values;
    }

    public static (string? Vid, string? Pid) UsbIds(string instanceId)
    {
        Match match = UsbPattern.Match(instanceId);
        return match.Success ? (match.Groups[1].Value.ToUpperInvariant(), match.Groups[2].Value.ToUpperInvariant())
            : (null, null);
    }

    public static DeviceChange[] Changes(ComDevice[] previous, ComDevice[] current)
    {
        var before = previous.ToDictionary(device => device.InstanceId, StringComparer.OrdinalIgnoreCase);
        var after = current.ToDictionary(device => device.InstanceId, StringComparer.OrdinalIgnoreCase);
        return previous.Where(device => !after.ContainsKey(device.InstanceId))
            .Select(device => new DeviceChange("removed", device))
            .Concat(current.Where(device => !before.ContainsKey(device.InstanceId))
                .Select(device => new DeviceChange("added", device)))
            .Concat(current.Where(device => before.TryGetValue(device.InstanceId, out var old) &&
                    (old with { InstanceId = device.InstanceId }) != device)
                .Select(device => new DeviceChange("changed", device)))
            .ToArray();
    }

    public static void Print(string state, ComDevice device)
    {
        static string Value(string? value) => string.IsNullOrWhiteSpace(value) ? "(not reported)" : value;
        Console.WriteLine($"\n[{state}] {device.Port}: {Value(device.Name)}");
        Console.WriteLine($"  Bus description:       {Value(device.BusDescription)}");
        Console.WriteLine($"  Reported manufacturer: {Value(device.ReportedManufacturer)}");
        Console.WriteLine($"  USB VID / PID:         {Value(device.Vid)} / {Value(device.Pid)}");
        Console.WriteLine($"  PnP instance:          {device.InstanceId}");
        Console.WriteLine($"  Parent instance:       {Value(device.ParentInstanceId)}");
        Console.WriteLine($"  USB location:          {Value(device.Location)}");
    }

    private static int PortNumber(string port) => int.TryParse(port.AsSpan(3),
        NumberStyles.None, CultureInfo.InvariantCulture, out int number) ? number : int.MaxValue;

    public static int SelfTest()
    {
        static void Check(bool condition, string label)
        {
            if (!condition) throw new InvalidOperationException($"Self-test failed: {label}");
        }
        var first = new ComDevice("COM3", "Fake USB (COM3)", "Synthetic interface", null,
            "303A", "1001", @"USB\VID_303A&PID_1001\FAKE-A", null, "FAKE-LOCATION");
        var second = first with { Port = "COM10", InstanceId = @"USB\VID_303A&PID_1001\FAKE-B" };
        Check(UsbIds(first.InstanceId) == ("303A", "1001"), "VID/PID extraction");
        Check(UsbIds(@"usb\vid_1a86&pid_7523\FAKE") == ("1A86", "7523"), "case normalization");
        Check(UsbIds(@"FTDIBUS\VID_0403+PID_6001+FAKE") == ("0403", "6001"), "bridge ID syntax");
        Check(UsbIds(@"BTHENUM\FAKE") == (null, null), "non-USB is not invented");
        Check(PortNumber("COM3") < PortNumber("COM10"), "numeric COM ordering");
        Check(Changes([first, second], [first]).Single() == new DeviceChange("removed", second), "unplug");
        Check(Changes([first], [first, second]).Single() == new DeviceChange("added", second), "replug");
        Check(Changes([first], [first]).Length == 0, "unchanged snapshot");
        var reassigned = first with { Port = "COM8" };
        Check(Changes([first], [reassigned]).Single() == new DeviceChange("changed", reassigned), "COM reassignment");
        Check(Changes([first], [second]).Length == 2, "replacement is remove plus add");
        Check(Changes([], []).Length == 0, "empty inventory");
        Check(Changes([first], [first with { InstanceId = first.InstanceId.ToLowerInvariant() }])
            .Length == 0, "case does not invent a change");
        Console.WriteLine("PASS: 12 synthetic checks. No WMI, registry or serial device accessed.");
        return 0;
    }
}
