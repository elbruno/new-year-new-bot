# Episode 1 code examples

These files support the public Episode 1 walkthrough. They show the API shape
used by the source example, not a claim about a released or installed firmware.
The presenter must confirm the installed firmware and its advertised
capabilities before the live action demo.

**Prepared for review. Not compiled or executed in this presentation-preparation session.**

## C++ firmware excerpts

The files in `firmware-snippets/` are short excerpts adapted from the real
firmware implementation. They are illustrative only, are not standalone
programs, and must not be compiled by themselves:

- `draw-lab.cpp` shows the display call used by `drawLab()`.
- `update-petting.cpp` shows the top-touch sampling and petting trigger logic.
- `http-endpoints.cpp` shows the registered status, action, and version routes.

## C# client and console

`csharp-console/` is a standalone .NET 11 console application. It reads the
firmware version and status, displays the returned capabilities, then waits for
the presenter to choose a non-motion expression (`Smile`, `Wink`, or `Neutral`).
The client sends a single correlated action request only after that choice.
It does not retry an uncertain POST automatically.

From the repository root, enter the robot's private IPv4 address at the prompt:

```powershell
$robotIp = Read-Host "Robot private IPv4 address"
dotnet run --project samples/new-year-new-bot-episode-1/csharp-console/RobotClient.csproj -- $robotIp
```

The robot API is unauthenticated. Use it only on a trusted, isolated LAN, and
never expose it to the public Internet. Keep the private address off streamed
screens. A private IPv4 address does not establish device identity or firmware
compatibility.

For official Episode 1 resources, see
[New Year, New Bot - Episode 1 resources](../../docs/presentations/New-Year-New-Bot-Episode-1-Links.md).
