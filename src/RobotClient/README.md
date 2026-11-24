# Robot client and console demo

`StackChan.RobotClient` is the reusable C# HTTP client. `StackChan.ConsoleDemo`
is the interactive application built on it. Both target .NET 11 and come from
the public firmware 0.5.1 source snapshot.

From the repository root, run:

```powershell
$robotIp = Read-Host "Robot private IPv4 address"
dotnet run --project .\src\RobotClient\StackChan.ConsoleDemo -- --ip $robotIp
```

In the console, choose **Expressions & Actions**, then select a non-motion
expression such as Smile, Wink, or Neutral only when the robot advertises it.
The client checks capabilities and correlates each command; it never
automatically retries an uncertain action request.

The robot API is unauthenticated. Use it only on a trusted, isolated LAN and
never expose it to the public Internet. Treat the address and terminal output
as private.

The console depends on Spectre.Console 0.57.2, licensed under MIT:
[upstream license](https://github.com/spectreconsole/spectre.console/blob/main/LICENSE.md).
The public source does not assign a separate license to this project code.
