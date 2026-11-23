using System.Text.Json;
using Episode1;

if (args.Length != 1)
{
    Console.Error.WriteLine("Supply one private IPv4 address.");
    return 2;
}

using var handler = new SocketsHttpHandler
{
    AllowAutoRedirect = false,
    UseProxy = false,
    ConnectTimeout = TimeSpan.FromSeconds(3)
};
using var http = new HttpClient(handler) { Timeout = TimeSpan.FromSeconds(5) };

try
{
    var robot = new RobotClient(http, args[0]);
    var version = await robot.GetVersionAsync();
    var status = await robot.GetStatusAsync();
    Console.WriteLine($"Firmware: {version.FirmwareVersion}; protocol: {version.ProtocolVersion}");
    Console.WriteLine($"Boot: {status.BootId}");
    Console.WriteLine($"Capabilities: {string.Join(", ", status.Capabilities)}");

    Console.WriteLine("Choose an expression: [1] Smile  [2] Wink  [3] Neutral  [Q] Quit");
    RobotAction? action = Console.ReadKey(intercept: true).Key switch
    {
        ConsoleKey.D1 or ConsoleKey.NumPad1 => RobotAction.Smile,
        ConsoleKey.D2 or ConsoleKey.NumPad2 => RobotAction.Wink,
        ConsoleKey.D3 or ConsoleKey.NumPad3 => RobotAction.Neutral,
        _ => null
    };
    Console.WriteLine();
    if (action is null) return 0;

    var result = await robot.PerformAsync(action.Value);
    var receipt = result.Value.Command!;
    Console.WriteLine($"Action: {receipt.Action}; state: {receipt.State}; HTTP {(int)result.StatusCode}");
    return 0;
}
catch (CommandOutcomeUncertainException ex)
{
    Console.Error.WriteLine(ex.Message);
    return 1;
}
catch (Exception ex) when (ex is ArgumentException or HttpRequestException or TaskCanceledException or
                           InvalidDataException or JsonException or InvalidOperationException)
{
    Console.Error.WriteLine($"Robot request failed: {ex.Message}");
    return 1;
}
