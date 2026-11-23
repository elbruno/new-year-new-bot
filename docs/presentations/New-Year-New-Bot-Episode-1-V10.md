# New Year, New Bot - Episode 1

**Official session title:** Bring Your AI Desk Robot to Life  
**Session date:** November 24, 2026  
**Runbook revision:** V10, Markdown only  
**Final PowerPoint:** V09, unchanged  
**Target duration:** 55:00  
**Public repository:** https://github.com/elbruno/new-year-new-bot  
**Public episode resources:** https://github.com/elbruno/new-year-new-bot/blob/main/docs/presentations/New-Year-New-Bot-Episode-1-Links.md

> This Markdown file is the presenter guide. V09 remains the approved visual deck; no V10 PowerPoint is being created. Public code links below were checked against the merged public `main` files. The examples were not compiled or executed in this presentation-preparation session.

## Presenter navigation (local only)

- **Deck:** Open `docs/presentations/New-Year-New-Bot-Episode-1-V09.pptx`. Keep its 18 visual slides unchanged.
- **Editor:** Open the public repository root, then use the exact sample paths and line links in each slide's `CODE / MEDIA / PATHS`.
- **Terminal:** Slide 17 uses the PowerShell launch command in its code section. Enter the robot's private IPv4 address only at the prompt; do not show it on the stream.
- **Camera:** Use the existing rehearsed OBS scene showing terminal and robot camera side by side. The local scene label is not recorded in this runbook; select the already-configured scene, not a guessed URL.
- **Video:** Play the complete embedded 2:57 eShopLite video on slide 7 with audio. The `Bot - StackChan-eShopLite-02 - 25%.mp4` file is the 480x270 preview, not the full-resolution show copy.
- **Firmware readiness:** The latest recorded physical evidence is for installed firmware 0.3.4 on November 21, 2026. The operator confirmed avatar expressions and the three-stage top-touch petting/audio sequence on that version (`docs/STATUS.md`, entry dated 2026-11-21). The private release catalog lists 0.5.0 and 0.5.1 as `draft`, with 0.5.1 as the current product version; neither draft status establishes installation or physical validation. Confirm the exact installed version and advertised `smile`, `wink`, or `neutral` capability with the firmware owner before selecting an action. If that confirmation is unavailable, show only the version/status response and explain that the action demo is pending.
- **Demo scope:** The public C# sample has no servo movement actions. Do not add a movement demo to this run.

## Slide-by-slide runbook

## Slide 01 | New Year, New Bot | Episode 1: Bring Your AI Desk Robot to Life

### Duration and cumulative time

1:30 | 01:30 / 55:00

### ON SCREEN

Start in the robot camera scene, then switch to the opening title slide.

### SAY (EN)

"Meet my newest coworker. It is small, has a face, and, unlike most agents, it can actually sit on my desk. Welcome to New Year, New Bot, Episode 1: Bring Your AI Desk Robot to Life. Today we start with the robot itself, the C++ firmware, and controlling it from C#."

### ACTION / TIMING

- 0:00-0:35: Start with a close-up of Robot 2 on camera and show its face. Do not trigger an action unless the exact installed firmware and behavior have already been confirmed.
- 0:35-1:00: Switch to the title slide and introduce yourself briefly.
- 1:00-1:30: Set expectations: real code, a prerecorded factory demo, and a C# demo. Move directly to the motivation.

### CODE / MEDIA / PATHS

Camera: real robot; PowerPoint: slide 1. No source file.

### TRANSITION

"So why did I want an agent that has a physical presence?"

## Slide 02 | Why I want a physical AI agent

### Duration and cumulative time

3:00 | 04:30 / 55:00

### ON SCREEN

Slide 2, conceptual desk-companion illustration and three motivations.

### SAY (EN)

"We all have agents living in terminals, editors, and browser tabs. I wanted something that could stay on my desk. Not another chat window: a face, a voice and physical reactions. The fun part is connecting familiar developer tools to something that really moves and reacts."

### ACTION / TIMING

- 0:00-1:15: Tell the personal motivation in your own words, using the illustration as a metaphor rather than claiming it shows StackChan hardware.
- 1:15-2:15: Talk through presence, face and voice, and actions.
- 2:15-3:00: Point back to the live robot. Explain that the series will make those ideas real, step by step.

### CODE / MEDIA / PATHS

PowerPoint: slide 2; optionally cut briefly to the robot camera, then return. No code.

### TRANSITION

"Let me show you the actual device we chose."

## Slide 03 | Meet StackChan

### Duration and cumulative time

1:30 | 06:00 / 55:00

### ON SCREEN

Slide 3, real desk photograph with two StackChan units and a retail box.

### SAY (EN)

"This is StackChan, a small desktop robot from M5Stack. The screen becomes its face, and it has hardware for audio and interaction. I have two here: one running the factory experience, and another running our firmware."

### ACTION / TIMING

- 0:00-0:45: Identify the photo, box, and two devices. Do not claim to be the photographer unless confirmed.
- 0:45-1:15: Show the two physical robots together on camera if available. State clearly which is Robot 1 and Robot 2.
- 1:15-1:30: Return to PowerPoint.

### CODE / MEDIA / PATHS

PowerPoint: slide 3 -> robot camera -> PowerPoint. No code.

### TRANSITION

"Before we talk firmware, here is the hardware we have to work with."

## Slide 04 | What's inside StackChan?

### Duration and cumulative time

1:30 | 07:30 / 55:00

### ON SCREEN

Slide 4, hardware cards and M5Stack product photo.

### SAY (EN)

"At the hardware level, we have a display, sound, touch sensing, and a movable head. There is an ESP32-S3 inside, Wi-Fi and battery power. Those components are available, but the firmware decides which ones we can actually use. That is the important distinction."

### ACTION / TIMING

- 0:00-1:00: Scan the display, audio, touch, and movement cards. Do not deep dive into every specification.
- 1:00-1:30: Contrast factory firmware on Robot 1 with our custom firmware on Robot 2.

### CODE / MEDIA / PATHS

PowerPoint: slide 4. Hardware reference: https://docs.m5stack.com/en/StackChan/

### TRANSITION

"First, what did StackChan do out of the box?"

## Slide 05 | The factory experience

### Duration and cumulative time

1:30 | 09:00 / 55:00

### ON SCREEN

Slide 5, factory firmware -> manufacturer cloud -> conversation -> voice and face.

### SAY (EN)

"Robot 1 runs its factory firmware. It talks to the manufacturer cloud, which handles the conversation and returns speech and facial expressions. That is not our backend. But it provided a very convenient starting point."

### ACTION / TIMING

- 0:00-0:50: Trace the diagram from robot to manufacturer cloud and back.
- 0:50-1:15: Clarify that this is the factory cloud flow, not the local HTTP API we will build.
- 1:15-1:30: Introduce extensibility through tools.

### CODE / MEDIA / PATHS

PowerPoint: slide 5. No code or live network dependency.

### TRANSITION

"Now, we wanted this robot to do more than talk. Enter MCP and C#."

## Slide 06 | Extending StackChan with MCP and C#

### Duration and cumulative time

1:40 | 10:40 / 55:00

### ON SCREEN

Slide 6, original .NET Conf MCP architecture, unchanged.

### SAY (EN)

"The factory conversation can call external MCP tools. Our .NET MCP server provides the tools, and a connector lets the conversation invoke them. The robot remains the same. We are extending its capabilities, not flashing new firmware in this part of the story."

### ACTION / TIMING

- 0:00-0:35: Start at the StackChan and factory conversation components.
- 0:35-1:15: Follow the connection to the .NET MCP server, custom tools, and results returning to the cloud.
- 1:15-1:40: Let any configured automatic animations complete, then transition to the video. Do not rely on multiple manual reveals.

### CODE / MEDIA / PATHS

PowerPoint: slide 6. This is a conceptual diagram, not a code walkthrough.

### TRANSITION

"Here is the complete example with eShopLite."

## Slide 07 | eShopLite meets StackChan

### Duration and cumulative time

3:40 | 14:20 / 55:00

### ON SCREEN

Slide 7, reserved area for the complete prerecorded 2:57 demo.

### SAY (EN)

"This is eShopLite, a sample outdoor-products application with semantic search. Here is the factory StackChan experience extended with our MCP tools. This is prerecorded, so you can see the full flow without relying on a live cloud connection."

### ACTION / TIMING

- 0:00-0:20: Introduce eShopLite and state that the video is prerecorded.
- 0:20-3:17: Play the entire 2:57 video with its audio. Do not narrate over it. Use the high-resolution version embedded in V09.
- 3:17-3:40: Summarize briefly how the robot used MCP tools, including the application and traces shown.

### CODE / MEDIA / PATHS

PowerPoint: slide 7 -> embedded video. `Bot - StackChan-eShopLite-02 - 25%.mp4` is the 480x270 preview only.

### TRANSITION

"That was the factory experience. Now I want to control the robot itself."

## Slide 08 | New Year, New Bot: taking control

### Duration and cumulative time

0:50 | 15:10 / 55:00

### ON SCREEN

Slide 8, section-transition statement.

### SAY (EN)

"We started by connecting tools to a robot with factory firmware. But for this project I wanted to own the device behavior: the face, touch, audio, movement, and the APIs that control them. That brings us to New Year, New Bot."

### ACTION / TIMING

- 0:00-0:30: Make the contrast between extending the factory cloud and developing custom firmware.
- 0:30-0:50: Give the three-episode framing before showing the original architecture.

### CODE / MEDIA / PATHS

PowerPoint: slide 8; no code.

### TRANSITION

"This is the full vision for the three episodes."

## Slide 09 | New Year, New Bot: the three-episode architecture

### Duration and cumulative time

2:00 | 17:10 / 55:00

### ON SCREEN

Slide 9, original .NET Conf architecture, unchanged. Robot on the left; .NET backend in the middle; agents, models, and integrations to the right; Aspire spans orchestration and observability.

### SAY (EN)

"This is what we want to build across three episodes. Today is the left-hand side: StackChan, our C++ firmware, HTTP APIs, and the C# code that talks to the robot. In Episode 2, we build the .NET and Aspire backend. In Episode 3, we explore agents, tools, and models. Those future integrations are the roadmap, not things I am claiming to demo today."

### ACTION / TIMING

- 0:00-0:50: Point only to the robot area on the left for Episode 1.
- 0:50-1:20: Point to ASP.NET Core and Aspire in the middle for Episode 2.
- 1:20-1:45: Indicate agents, models, and integrations on the right for Episode 3.
- 1:45-2:00: Return attention to the robot. Do not attempt to explain every small label.

### CODE / MEDIA / PATHS

PowerPoint: slide 9. Keep the original diagram and its existing links.

### TRANSITION

"Let us zoom in on the left-hand side: the firmware."

## Slide 10 | Our own firmware: C++ on ESP32-S3

### Duration and cumulative time

2:20 | 19:30 / 55:00

### ON SCREEN

Slide 10, overview of Robot 2 custom firmware, animated .NET avatar, Wi-Fi, status, and version.

### SAY (EN)

"Robot 2 runs our firmware on its ESP32-S3. We control how it draws expressions and responds to events. Wi-Fi and the HTTP API give us a bridge to .NET. The goal was a small, understandable firmware surface, not another giant framework."

### ACTION / TIMING

- 0:00-0:50: Explain Robot 2 versus the factory firmware and name the main components.
- 0:50-1:40: Show Robot 2 on camera with the .NET avatar if the camera is ready. Do not claim a current-version behavior without confirmation.
- 1:40-2:20: Bring VS Code into view briefly to identify the public firmware excerpts, then return to the slide or move to slide 11.

### CODE / MEDIA / PATHS

Public examples: `samples/new-year-new-bot-episode-1/firmware-snippets/draw-lab.cpp`, lines 1-5, https://github.com/elbruno/new-year-new-bot/blob/main/samples/new-year-new-bot-episode-1/firmware-snippets/draw-lab.cpp#L1-L5 ; and `http-endpoints.cpp`, lines 1-21, https://github.com/elbruno/new-year-new-bot/blob/main/samples/new-year-new-bot-episode-1/firmware-snippets/http-endpoints.cpp#L1-L21 .

### TRANSITION

"Here are the capabilities implemented in the inspected source."

## Slide 11 | What can our firmware do?

### Duration and cumulative time

2:30 | 22:00 / 55:00

### ON SCREEN

Slide 11, four cards: display/avatar, audio, top-touch petting, and movement commands, all labeled implemented in inspected source.

### SAY (EN)

"The inspected source covers four useful areas: drawing a face, playing audio, reading the top touch sensors, and sending movement commands. I want to distinguish implementation from validation. The recorded physical evidence is for firmware 0.3.4; it does not establish behavior for the current draft. We will demonstrate only what the installed firmware and its advertised capabilities confirm."

### ACTION / TIMING

- 0:00-0:50: Walk through the four cards and distinguish hardware capability from source implementation.
- 0:50-1:35: On the robot camera, show the face and, only if version-confirmed and rehearsed, one non-motion expression.
- 1:35-2:30: Switch to VS Code and navigate to the published rendering excerpt as the lead-in to slide 12. Do not present audio or movement code that is not included in the public Episode 1 sample.

### CODE / MEDIA / PATHS

The public sample includes display lines in `draw-lab.cpp` (1-5), https://github.com/elbruno/new-year-new-bot/blob/main/samples/new-year-new-bot-episode-1/firmware-snippets/draw-lab.cpp#L1-L5 ; top-touch detection in `update-petting.cpp` (1-34), https://github.com/elbruno/new-year-new-bot/blob/main/samples/new-year-new-bot-episode-1/firmware-snippets/update-petting.cpp#L1-L34 ; and route registration in `http-endpoints.cpp` (1-21), https://github.com/elbruno/new-year-new-bot/blob/main/samples/new-year-new-bot-episode-1/firmware-snippets/http-endpoints.cpp#L1-L21 . Audio and movement are not part of these public excerpts.

### TRANSITION

"The first implementation is wonderfully simple: draw the face."

## Slide 12 | Drawing a Face with C++

### Duration and cumulative time

4:30 | 26:30 / 55:00

### ON SCREEN

Slide 12, C++ `drawLab()` excerpt and three explanatory cards.

### SAY (EN)

"This is the code that places the selected expression on the display. `labPixels(expression)` supplies the image pixels, and `M5.Display.pushImage` draws a 320 by 240 frame. Then we invalidate the status strip so it can be redrawn. Tiny code, very visible result."

### ACTION / TIMING

- 0:00-0:50: Explain the visible C++ excerpt on the slide.
- 0:50-1:15: Switch to VS Code in the public repository checkout.
- 1:15-3:30: Open `draw-lab.cpp` and explain the selected `drawLab()` display call; the excerpt omits the surrounding redraw condition and state handling.
- 3:30-4:00: Switch briefly to the robot camera to show a face or expression only if the installed firmware has been confirmed.
- 4:00-4:30: Return to the slide or VS Code and bridge to touch input.

### CODE / MEDIA / PATHS

Public path: `samples/new-year-new-bot-episode-1/firmware-snippets/draw-lab.cpp`, lines 1-5. Exact excerpt and GitHub source: https://github.com/elbruno/new-year-new-bot/blob/main/samples/new-year-new-bot-episode-1/firmware-snippets/draw-lab.cpp#L1-L5 .

### TRANSITION

"Now let us take input from the robot itself: petting it."

## Slide 13 | Petting the Robot with C++

### Duration and cumulative time

4:30 | 31:00 / 55:00

### ON SCREEN

Slide 13, shortened `updatePetting()` C++ excerpt.

### SAY (EN)

"The top sensors give us a simple form of physical input. The code requires a fresh sample, a settle period, a release, and consecutive touch samples before reacting. That helps prevent a single noisy reading from triggering an interaction. The rest of the method handles the reaction sequence."

### ACTION / TIMING

- 0:00-0:45: Explain the fresh top-sensor sample check and early returns.
- 0:45-1:10: Move into VS Code and open the actual `updatePetting()` excerpt.
- 1:10-3:20: Walk through the 500-millisecond settle, release samples, two touch samples, and the call into the reaction sequence.
- 3:20-4:05: Switch to the robot camera and show a petting response only if the installed version is confirmed and the presenter has rehearsed it.
- 4:05-4:30: Return to PowerPoint.

### CODE / MEDIA / PATHS

Public path: `samples/new-year-new-bot-episode-1/firmware-snippets/update-petting.cpp`, complete excerpt lines 1-34; `updatePetting()` is lines 8-34. GitHub source: https://github.com/elbruno/new-year-new-bot/blob/main/samples/new-year-new-bot-episode-1/firmware-snippets/update-petting.cpp#L1-L34 .

### TRANSITION

"The robot now has behavior. How do we reach it from C#?"

## Slide 14 | From firmware to HTTP APIs

### Duration and cumulative time

5:00 | 36:00 / 55:00

### ON SCREEN

Slide 14, pipeline from C# client to HTTP API, firmware, and behavior with four inspected routes.

### SAY (EN)

"The robot exposes a small HTTP surface on our local Wi-Fi. A .NET client can ask for status or version, and post an action. This is HTTP on a trusted local network, not an internet-facing production API. The client checks advertised capabilities and correlates an action with the boot and command IDs."

### ACTION / TIMING

- 0:00-1:00: Explain the displayed routes, distinguishing GETs from POST.
- 1:00-1:25: Switch to VS Code.
- 1:25-3:30: Open the route-registration excerpt. Explain that the public sample shows the status, action, and version routes; the slide's network route is not included in this focused excerpt.
- 3:30-4:30: Open the C# `PerformAsync()` path and connect its four-field JSON request to `POST /api/actions`. Do not suggest a motion action is part of this sample.
- 4:30-5:00: Return to PowerPoint.

### CODE / MEDIA / PATHS

C++ route excerpt: `samples/new-year-new-bot-episode-1/firmware-snippets/http-endpoints.cpp`, lines 1-21, https://github.com/elbruno/new-year-new-bot/blob/main/samples/new-year-new-bot-episode-1/firmware-snippets/http-endpoints.cpp#L1-L21 . C# action request: `samples/new-year-new-bot-episode-1/csharp-console/RobotClient.cs`, lines 60-129, https://github.com/elbruno/new-year-new-bot/blob/main/samples/new-year-new-bot-episode-1/csharp-console/RobotClient.cs#L60-L129 .

### TRANSITION

"One small detour: how we build and ship that firmware with Copilot."

## Slide 15 | Firmware with GitHub Copilot App

### Duration and cumulative time

2:00 | 38:00 / 55:00

### ON SCREEN

Slide 15, already-correct five-step Robot Delivery Monitor workflow. Do not change its design.

### SAY (EN)

"Firmware development is different from ordinary .NET code, because at the end you need to build, package and flash something onto a physical device. GitHub Copilot helps with the workflow: checks, compile, package, deploy, and verify on the robot. This slide illustrates the process; it is not a live screenshot of an agent running right now."

### ACTION / TIMING

- 0:00-1:15: Walk across the five existing cards from left to right.
- 1:15-1:45: Explain why firmware delivery is different from cloud deployment.
- 1:45-2:00: Transition immediately to the C# application. Do not start a build or deployment.

### CODE / MEDIA / PATHS

PowerPoint: slide 15 only. No compilation, flashing, robot request, or Copilot action during this presentation-preparation session.

### TRANSITION

"We now have a robot API. Let us write very little C# to use it."

## Slide 16 | Control StackChan with C#

### Duration and cumulative time

7:00 | 45:00 / 55:00

### ON SCREEN

Slide 16, paired excerpts from the typed C# client and console application.

### SAY (EN)

"The robot client library is intentionally small. `GetVersionAsync()` and `GetStatusAsync()` read typed responses. `PerformAsync()` checks the capability, sends one correlated expression request, and verifies the returned command receipt. The console app is just a friendly way to choose Smile, Wink, or Neutral."

### ACTION / TIMING

- 0:00-1:00: Use the slide to distinguish the client library from the console UI.
- 1:00-1:20: Switch to VS Code with the public repository root open.
- 1:20-3:20: Show `RobotClient.cs`, `GetVersionAsync()` and `GetStatusAsync()`. Explain the firmware version, boot identity, and advertised capabilities.
- 3:20-5:15: Show `PerformAsync()` and how an expression maps to `POST /api/actions`, including command/boot correlation and the no-automatic-retry behavior.
- 5:15-6:25: Open `Program.cs` and show how the user selects Smile, Wink, or Neutral.
- 6:25-7:00: Switch to the prepared demo terminal/camera scene.

### CODE / MEDIA / PATHS

Client version/status: `samples/new-year-new-bot-episode-1/csharp-console/RobotClient.cs`, lines 44-57, https://github.com/elbruno/new-year-new-bot/blob/main/samples/new-year-new-bot-episode-1/csharp-console/RobotClient.cs#L44-L57 . Client action: same file, lines 60-129, https://github.com/elbruno/new-year-new-bot/blob/main/samples/new-year-new-bot-episode-1/csharp-console/RobotClient.cs#L60-L129 . Console selection: `samples/new-year-new-bot-episode-1/csharp-console/Program.cs`, lines 18-44, https://github.com/elbruno/new-year-new-bot/blob/main/samples/new-year-new-bot-episode-1/csharp-console/Program.cs#L18-L44 .

### TRANSITION

"Now we will try the full path, from the C# menu to a real robot reaction."

## Slide 17 | Live demo: StackChan from C#

### Duration and cumulative time

7:00 | 52:00 / 55:00

### ON SCREEN

Slide 17 has three steps and no fake camera placeholder. During the live demo, use the real OBS scene with terminal and robot side by side.

### SAY (EN)

"We have seen the firmware, the HTTP endpoints, and the .NET client. Now we will connect those pieces. First, read status and version. If the installed firmware reports the supported expression capability, select one expression and observe the response. The C# code does not need to know how the display is driven."

### ACTION / TIMING

- 0:00-0:40: Explain the three steps on slide 17.
- 0:40-1:10: Switch to the existing OBS scene with terminal and robot camera.
- 1:10-2:30: From the public repository root, run the command below. The console reads version and status before presenting its expression menu.
- 2:30-4:20: Confirm the displayed firmware version and capability with the firmware owner. If confirmed, the presenter may select one non-motion expression. If not confirmed, do not select an action; show the status/version output and continue with the source walkthrough.
- 4:20-5:45: Explain the actual response receipt and the client correlation checks on screen. Do not repeat an uncertain POST.
- 5:45-6:30: Explain the end-to-end path: C# client, HTTP API, firmware, and face.
- 6:30-7:00: Switch back to the closing slide.

### CODE / MEDIA / PATHS

Project: `samples/new-year-new-bot-episode-1/csharp-console/RobotClient.csproj`, lines 1-8, https://github.com/elbruno/new-year-new-bot/blob/main/samples/new-year-new-bot-episode-1/csharp-console/RobotClient.csproj#L1-L8 . Console entry point: `Program.cs`, lines 18-44, https://github.com/elbruno/new-year-new-bot/blob/main/samples/new-year-new-bot-episode-1/csharp-console/Program.cs#L18-L44 . Client request/correlation: `RobotClient.cs`, lines 60-129, https://github.com/elbruno/new-year-new-bot/blob/main/samples/new-year-new-bot-episode-1/csharp-console/RobotClient.cs#L60-L129 .

PowerShell command, from the public repository root:

```powershell
$robotIp = Read-Host "Robot private IPv4 address"
dotnet run --project samples/new-year-new-bot-episode-1/csharp-console/RobotClient.csproj -- $robotIp
```

The project path and command argument shape were checked statically; this preparation session did not run the command. Keep the address off the stream. The API is unauthenticated and is for a trusted, isolated LAN only.

### TRANSITION

"That is Episode 1: we now have a programmable physical edge for the companion."

## Slide 18 | Bring Your AI Desk Robot to Life

### Duration and cumulative time

3:00 | 55:00 / 55:00

### ON SCREEN

Final slide: official episode title, series/episode identifier, and public GitHub repository.

### SAY (EN)

"That is Episode 1: Bring Your AI Desk Robot to Life. We gave the robot its own firmware, exposed a small HTTP API, and used C# to control it. In Episode 2, Build Its AI Brain with .NET and Aspire, we will work on the backend. Episode 3, Give Your Bot an Agentic Brain, adds agents and tools. The project and episode resources are on GitHub. Thanks for joining!"

### ACTION / TIMING

- 0:00-1:20: Recap the three things actually shown: C++ firmware paths, the local HTTP API, and the C# client. Do not claim planned backend or agent features are already deployed.
- 1:20-1:50: Mention the official Episode 2 and Episode 3 titles briefly.
- 1:50-3:00: Show the public repository and Episode 1 resources page, then take one short question or use the remaining time as buffer.

### CODE / MEDIA / PATHS

Public repository: https://github.com/elbruno/new-year-new-bot  
Episode 1 resources: https://github.com/elbruno/new-year-new-bot/blob/main/docs/presentations/New-Year-New-Bot-Episode-1-Links.md  
Official series: https://elbruno.github.io/new-year-new-bot/

### TRANSITION

End of Episode 1.
