# Which device is on this COM port?

A standalone **.NET 10 file-based C# app for Windows 10/11**. No `.csproj`,
RobotHub, Aspire, PowerShell subprocess, admin prompt or serial connection is
required. The first run restores `System.Management` from NuGet.

```powershell
Set-Location tools\serial-ports
dotnet --version
# A stable 10.0.x SDK. Verified locally with 10.0.402.
dotnet run SerialPorts.cs
dotnet run SerialPorts.cs -- --watch
dotnet run SerialPorts.cs -- --json
dotnet run SerialPorts.cs -- --watch --json
dotnet run SerialPorts.cs -- --self-test
```

Run from this directory: its `global.json` selects a stable .NET 10 SDK. The C# file explicitly
targets `net10.0` and disables Native AOT because the sample uses WMI and
reflection-based JSON serialization. When copied outside a repository with SDK
pins, only `SerialPorts.cs` and an installed .NET 10 SDK are needed.

## Information shown

| Field | Meaning and limitation |
| --- | --- |
| COM port | Windows' current assignment, not a permanent identity |
| Name | Friendly name, often just `USB Serial Device (COM7)` |
| Bus description | Device-reported text, such as `USB JTAG/serial debug unit` |
| Reported manufacturer | Windows/driver metadata, not proof of the board manufacturer |
| VID / PID | USB vendor/product IDs, possibly belonging to a bridge shared by many boards |
| PnP instance | Windows device-instance identity; may be location-dependent |
| Parent instance | Parent USB device; may contain a serial or chip identifier |
| USB location | Topology hint for finding a physical port/hub |

Missing optional fields say `(not reported)` in text and `null` in JSON.
WMI/permission failures return exit code 1 with an explicit stderr message.
Invalid arguments or unsupported OS discovery return 2; success and Ctrl+C
cancellation return 0.

`--json` emits one camel-case JSON object, with `kind: "snapshot"` and
`devices: [...]`. Watch JSON emits that snapshot, then one JSON object per
`added`, `removed` or `changed` event, containing a `device`. Status messages
never mix with JSON on stdout.

## Map two identical robots

1. Run `dotnet run SerialPorts.cs -- --watch`.
2. Disconnect only one USB cable. The `REMOVED` entry identifies its current COM
   port. This is a physical action by you, not performed by the utility.
3. Reconnect the same unit and inspect `ADDED`. Windows may assign another COM.
4. Label the physical unit and repeat with the other cable.
5. Use the observed ports in your application's external configuration.

Two devices with VID `303A` and PID `1001` can expose the same Espressif interface
while being different products. **Do not infer StackChan, CoreS3 or a safe
firmware target from those IDs.** Parent IDs help recognize a unit but are not
guaranteed universal serial numbers. Moving hubs, changing firmware/USB mode or
using a device without a serial can change identities.

Watch polls approximately once per second, plus query time. Very fast
disconnect/reconnect cycles between snapshots may not be observed. Ctrl+C stops
the loop; an in-flight Windows metadata query may take time to return.
A missing WMI device or deleted registry key during discovery triggers one
explicitly reported snapshot retry. Other errors, or a failed retry, stop with
exit code 1 instead of pretending the inventory is empty.

## Safety, testing and reuse

The app only reads WMI Plug and Play metadata and the `PortName` registry value.
It never creates `SerialPort`, opens a COM port, toggles DTR/RTS, resets a board,
writes to the registry, changes a driver or flashes firmware.

`--self-test` uses synthetic devices, without WMI, registry or hardware access.
It covers ID extraction, numeric ordering, unplug/replug, COM reassignment,
replacement, unchanged/empty snapshots and case-insensitive instance IDs.
The synthetic checks and live Windows enumeration were verified locally with .NET 10.
Physical unplug/replug still requires a person and is not claimed as an
automated hardware test.

**Before sharing output or recording:** redact real parent/instance identifiers,
MAC-like values and location details. The tool writes no files by itself, but
the terminal, redirected JSON and screenshots can contain unique identifiers.

This folder is self-contained: copy `SerialPorts.cs`, `global.json` and this
README to use the sample independently. Do not include real machine output in
shared copies.
