# New Year, New Bot - Episode 1: Bring Your AI Desk Robot to Life

**Official session:** Bring Your AI Desk Robot to Life
**Date:** November 24, 2026

Episode 1 introduces the StackChan desk robot, its C++ firmware, and the C#
client used to inspect its status and request supported expressions.

## Watch and follow

- [Official New Year, New Bot series website](https://elbruno.github.io/new-year-new-bot/)
- [Episode 1 event page](https://aka.ms/newyearnewbot/1)
- [Episode 1 livestream recording](https://www.youtube.com/watch?v=L-zaWiyjDEU)
- [Public project repository](https://github.com/elbruno/new-year-new-bot)

## Firmware 0.5.1 source

- [Complete PlatformIO source project](../../src/firmware/usb-avatar/)
- [Firmware build instructions and pinned dependencies](../../src/firmware/usb-avatar/README.md)
- [Firmware HTTP API Reference](https://github.com/elbruno/new-year-new-bot/blob/main/docs/api/firmware-http-api.md): Complete reference for the StackChan firmware HTTP endpoints, including methods, request/response contracts, build availability, and PowerShell examples.

The source is the committed 0.5.1 draft snapshot. It is not a coordinated
release or evidence that the files match an installed firmware image.

## C# client and console

- [Reusable C# robot client library](../../src/RobotClient/StackChan.RobotClient/)
- [Interactive C# console application](../../src/RobotClient/StackChan.ConsoleDemo/)
- [Client overview and console instructions](../../src/RobotClient/README.md)

From the repository root, enter the robot's private IPv4 address when
prompted, or pass it directly:

```powershell
$robotIp = Read-Host "Robot private IPv4 address"
dotnet run --project .\src\RobotClient\StackChan.ConsoleDemo -- --ip $robotIp
```

Choose **Expressions & Actions** and select a non-motion expression only when
the connected firmware advertises that capability. The local robot API is
unauthenticated; use it only on a trusted, isolated LAN.

## Documentation

- [M5Stack StackChan documentation](https://docs.m5stack.com/en/StackChan/)
- [M5Stack factory firmware guide](https://docs.m5stack.com/en/StackChan/#factory-firmware): covers StackChan World setup, the XiaoZhi-powered AI agent, and MCP configuration.
- [M5Stack factory firmware source](https://github.com/m5stack/StackChan): device firmware, mobile app, and server.
- [eShopLite](https://github.com/azure-Samples/eshoplite): the sample application used in the prerecorded StackChan + MCP demonstration.
- [M5Stack documentation portal](https://docs.m5stack.com/en/)
- [Espressif ESP32-S3 documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/)
- [.NET documentation](https://learn.microsoft.com/en-us/dotnet/)
- [C# guide](https://learn.microsoft.com/en-us/dotnet/csharp/)
- [`HttpClient` networking guidance](https://learn.microsoft.com/en-us/dotnet/fundamentals/networking/http/httpclient)

The factory cloud conversation and MCP experience are separate from this
project's custom C++ firmware and its local HTTP API.

## Future episodes

- [Episode 2: Build Its AI Brain with .NET and Aspire](https://aka.ms/newyearnewbot/2)
- [Episode 3: Give Your Bot an Agentic Brain](https://aka.ms/newyearnewbot/3)
- [Series website and updates](https://elbruno.github.io/new-year-new-bot/)
