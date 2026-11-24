// Goal: Provide an interactive console for exploring the robot.
// Responsibilities: Present menus, select actions, and report results.
// How it works:
// 1. Startup resolves the address and creates the reusable robot client.
// 2. Menu choices dispatch operations through that client.
// 3. The console displays responses and warns about uncertain outcomes.
using System.Runtime.CompilerServices;
using Spectre.Console;
using StackChan.RobotClient;

[assembly: InternalsVisibleTo("StackChan.RobotClient.Tests")]

// The console is a presentation layer over the reusable HTTP client.
return await Demo.RunAsync(args);

internal static class Demo
{
    private static readonly CancellationTokenSource Shutdown = new();
    private static string connection = "Not connected";
    private static string firmware = "Unknown";
    private static readonly ImageListCache ImageCache = new();
    private const string Back = "Back";
    private const string RefreshImages = "Refresh (F)";

    internal enum MainMenu { Expressions, Sounds, Motion, Images, Text, RGB, Screen, Status, Sensors, Demos, Reconnect, Stop, Exit }

    internal static string MainMenuLabel(MainMenu item) => item switch
    {
        MainMenu.Expressions => "😊 Expressions & Actions",
        MainMenu.Sounds => "🔊 Sounds & WAV Playback",
        MainMenu.Motion => "🤖 Motion & Gestures",
        MainMenu.Images => "🖼️ Images from microSD",
        MainMenu.Text => "📝 Temporary Text",
        MainMenu.RGB => "🌈 RGB Light",
        MainMenu.Screen => "💡 Screen & Brightness",
        MainMenu.Status => "📊 Robot Status",
        MainMenu.Sensors => "🔎 Sensors & Petting Diagnostics",
        MainMenu.Demos => "🎬 Sequential C# Demos",
        MainMenu.Reconnect => "🔄 Reconnect",
        MainMenu.Stop => "🛑 STOP",
        MainMenu.Exit => "🚪 Exit",
        _ => throw new ArgumentOutOfRangeException(nameof(item))
    };

    // Resolve the robot address, create the client, then dispatch menu selections.
    public static async Task<int> RunAsync(string[] args)
    {
        string? address;
        try { address = GetAddress(args); }
        catch (ArgumentException ex) { AnsiConsole.MarkupLine($"[red]{Markup.Escape(ex.Message)}[/]"); return 1; }
        if (address is null)
        {
            address = AnsiConsole.Ask<string>("Robot private IPv4 address:").Trim();
        }
        using var handler = new SocketsHttpHandler
        {
            AllowAutoRedirect = false,
            UseProxy = false,
            ConnectTimeout = TimeSpan.FromSeconds(3)
        };
        using var http = new HttpClient(handler) { Timeout = TimeSpan.FromSeconds(5) };
        StackChanClient robot;
        try { robot = new StackChanClient(http, $"http://{address}"); }
        catch (ArgumentException ex) { AnsiConsole.MarkupLine($"[red]{Markup.Escape(ex.Message)}[/]"); return 1; }
        Console.CancelKeyPress += (_, e) => { e.Cancel = true; Shutdown.Cancel(); };
        try
        {
            await RefreshAsync(robot);
            while (!Shutdown.IsCancellationRequested)
            {
                Header();
                MainMenu selection = AnsiConsole.Prompt(new SelectionPrompt<MainMenu>()
                    .Title("[purple]Choose an option[/]")
                    .UseConverter(MainMenuLabel)
                    .HighlightStyle(new Style(Color.Black, Color.Cyan, Decoration.Bold))
                    .PageSize(10).AddChoices(Enum.GetValues<MainMenu>()));
                if (selection == MainMenu.Exit) break;
                try
                {
                    switch (selection)
                    {
                        case MainMenu.Expressions: await ActionsAsync(robot); break;
                        case MainMenu.Sounds: await SoundsAsync(robot); break;
                        case MainMenu.Motion: await MotionAsync(robot); break;
                        case MainMenu.Images: await ImagesAsync(robot); break;
                        case MainMenu.Text: await TextAsync(robot); break;
                        case MainMenu.RGB: await RgbAsync(robot); break;
                        case MainMenu.Screen: await ScreenAsync(robot); break;
                        case MainMenu.Status: await StatusAsync(robot); break;
                        case MainMenu.Sensors: await SensorsAsync(robot); break;
                        case MainMenu.Demos: await DemosAsync(robot); break;
                        case MainMenu.Reconnect:
                            ImageCache.Invalidate();
                            await RefreshAsync(robot);
                            Message(connection);
                            break;
                        case MainMenu.Stop:
                            var stop = await robot.StopAsync(Shutdown.Token);
                            ImageCache.ObserveBoot(stop.Value.Receipt.BootId);
                            Message($"Stop: {stop.Value.Receipt.Outcome} (HTTP {(int)stop.StatusCode}).\n" +
                                "Controller receipt is not physical stop verification.");
                            break;
                    }
                }
                // An uncertain command outcome must not trigger an automatic retry.
                catch (RobotException ex)
                {
                    if (ex.Code is "unreachable" or "timeout") connection = "Disconnected";
                    Message($"[red]{Markup.Escape(ex.Message)}[/]" +
                        (ex.OutcomeUncertain ? "\n[yellow]Outcome uncertain. Do not retry automatically.[/]" : ""));
                }
                catch (HttpRequestException) { connection = "Disconnected"; Message("[red]Connection lost.[/]"); }
                catch (ArgumentException ex) { Message($"[red]{Markup.Escape(ex.Message)}[/]"); }
            }
        }
        catch (OperationCanceledException) when (Shutdown.IsCancellationRequested)
        {
            AnsiConsole.MarkupLine("[yellow]Canceled. An in-flight command may have an unknown outcome.[/]");
        }
        return 0;
    }

    private static string? GetAddress(string[] args)
    {
        if (args.Length != 0)
        {
            if (args.Length != 2 || args[0] != "--ip") throw new ArgumentException("Usage: --ip <private IPv4 address>");
            return args[1].Trim();
        }
        string? address = Environment.GetEnvironmentVariable("ROBOT_IP");
        return string.IsNullOrWhiteSpace(address) ? null : address.Trim();
    }

    private static async Task RefreshAsync(StackChanClient robot)
    {
        try
        {
            RobotStatus status = await robot.GetStatusAsync(Shutdown.Token);
            ImageCache.ObserveBoot(status.BootId);
            firmware = status.FirmwareVersion ?? (await robot.GetVersionAsync(Shutdown.Token)).FirmwareVersion;
            connection = "Connected";
        }
        catch (RobotException) { connection = "Disconnected"; firmware = "Unknown"; }
    }

    private static void Header()
    {
        AnsiConsole.Clear();
        AnsiConsole.Write(BuildHeader(firmware, connection));
    }

    internal static Panel BuildHeader(string version, string connectionState)
    {
        var face = new Markup("[purple]╭──╮[/]\n[purple]│[/][cyan]●●[/][purple]│[/]\n" +
            "[purple]│[/][cyan]ᴗ [/][purple]│[/]\n[purple]╰──╯[/]");
        string color = connectionState switch
        {
            "Connected" => "green",
            "Disconnected" => "red",
            _ => "yellow"
        };
        var text = new Markup($"[bold cyan]🤖 STACKCHAN CONTROL[/]\n🔃 Firmware: {Markup.Escape(version)}\n" +
            $"🛜 [{color}]{Markup.Escape(connectionState)}[/]\n🥳 New Year New Bot");
        var grid = new Grid().AddColumn(new GridColumn().Width(5)).AddColumn().AddRow(face, text);
        return new Panel(grid).Border(BoxBorder.Rounded).BorderColor(Color.Purple);
    }

    private static string Pick(string title, params string[] choices) =>
        AnsiConsole.Prompt(new SelectionPrompt<string>().Title($"[purple]{Markup.Escape(title)}[/]")
            .HighlightStyle(new Style(Color.Cyan)).PageSize(10).AddChoices(choices));

    private static void Message(string text)
    {
        AnsiConsole.MarkupLine(text);
        AnsiConsole.MarkupLine("[grey]Press Enter to return.[/]");
        Console.ReadLine();
    }

    internal static Table BuildSensorsView(SensorDiagnostics s, SensorDiagnostics? previous, int changes)
    {
        static string Number(int? value) => value?.ToString() ?? "Unavailable";
        static string Age(uint? value) => value is null ? "Unavailable" : $"{value} ms";
        static string Yes(bool value) => value ? "Yes" : "No";
        static string Highlight(string value, bool changed) =>
            changed ? $"[bold yellow]{Markup.Escape(value)} *[/]" : Markup.Escape(value);
        var table = new Table().Border(TableBorder.Rounded).Title("SENSORS & PETTING DIAGNOSTICS")
            .AddColumn("Reading").AddColumn("Value");
        void Row(string label, string value, bool changed = false) => table.AddRow(label, Highlight(value, changed));
        Row("Top Front / Middle / Back", $"{Number(s.Top.Front)} / {Number(s.Top.Middle)} / {Number(s.Top.Back)}",
            previous is not null && (s.Top.Front != previous.Top.Front || s.Top.Middle != previous.Top.Middle || s.Top.Back != previous.Top.Back));
        Row("Top initialized / available", $"{Yes(s.Top.Initialized)} / {Yes(s.Top.Available)}");
        Row("Top sample / age", $"{(s.Top.HasSample ? s.Top.Fresh ? "Fresh" : "Stale" : "Not sampled")} / {Age(s.Top.AgeMs)}");
        Row("Top error", s.Top.Error ?? "None");
        Row("Proximity / ALS CH0 / CH1", $"{Number(s.Light.Proximity)} / {Number(s.Light.Als0)} / {Number(s.Light.Als1)}",
            previous is not null && (s.Light.Proximity != previous.Light.Proximity || s.Light.Als0 != previous.Light.Als0 || s.Light.Als1 != previous.Light.Als1));
        Row("Light initialized / available", $"{Yes(s.Light.Initialized)} / {Yes(s.Light.Available)}");
        Row("Light sample / age", $"{(s.Light.HasSample ? s.Light.Fresh ? "Fresh" : "Stale" : "Not sampled")} / {Age(s.Light.AgeMs)}");
        Row("Light error", s.Light.Error ?? "None");
        if (s.Imu is { } imu)
        {
            static string Vector(ImuVector v) => v.HasSample
                ? $"{v.X:F3} / {v.Y:F3} / {v.Z:F3}" : "Unavailable";
            Row("IMU available / error", $"{Yes(imu.Available)} / {imu.Error ?? "None"}");
            Row("Acceleration X / Y / Z (g)", Vector(imu.AccelerationG));
            Row("Acceleration sample / age", $"{(imu.AccelerationG.HasSample ? imu.AccelerationG.Fresh ? "Fresh" : "Stale" : "Not sampled")} / {Age(imu.AccelerationG.AgeMs)}");
            Row("Gyroscope X / Y / Z (deg/s)", Vector(imu.GyroscopeDps));
            Row("Gyroscope sample / age", $"{(imu.GyroscopeDps.HasSample ? imu.GyroscopeDps.Fresh ? "Fresh" : "Stale" : "Not sampled")} / {Age(imu.GyroscopeDps.AgeMs)}");
        }
        else Row("IMU", "Unavailable (older firmware)");
        Row("Petting / sound / quiet", $"{Yes(s.Petting.Enabled)} / {Yes(s.Petting.SoundEnabled)} / {Yes(s.Petting.Quiet)}");
        Row("Eligible / reason", $"{Yes(s.Petting.Eligible)} / {s.Petting.IneligibleReason ?? "None"}",
            previous is not null && (s.Petting.Eligible != previous.Petting.Eligible || s.Petting.IneligibleReason != previous.Petting.IneligibleReason));
        Row("Settled / released", $"{Yes(s.Petting.Settled)} / {Yes(s.Petting.Released)}",
            previous is not null && (s.Petting.Settled != previous.Petting.Settled || s.Petting.Released != previous.Petting.Released));
        Row("Touch / release count", $"{s.Petting.TouchCount} / {s.Petting.ReleaseCount}",
            previous is not null && (s.Petting.TouchCount != previous.Petting.TouchCount || s.Petting.ReleaseCount != previous.Petting.ReleaseCount));
        Row("Petting state / stage", s.Petting.Active ? $"Active / {s.Petting.Stage}" : "Inactive",
            previous is not null && (s.Petting.Active != previous.Petting.Active || s.Petting.Stage != previous.Petting.Stage));
        Row("Sound reaction", s.Petting.SoundState, previous is not null && s.Petting.SoundState != previous.Petting.SoundState);
        Row("RGB active / duration", $"{Yes(s.Rgb.Active)} / {Age(s.Rgb.DurationMs)}");
        Row("RGB error", s.Rgb.Error ?? "None");
        Row("Observed top changes", changes.ToString());
        return table;
    }

    private static async Task SensorsAsync(StackChanClient robot)
    {
        var current = await robot.GetSensorsAsync(Shutdown.Token);
        AnsiConsole.Write(BuildSensorsView(current, null, 0));
        if (Pick("Diagnostics", "Start live monitoring", Back) == Back) return;
        if (Console.IsInputRedirected)
        {
            Message("[yellow]Live monitoring requires an interactive console.[/]");
            return;
        }
        int changes = 0;
        await AnsiConsole.Live(BuildSensorsView(current, null, changes)).StartAsync(async context =>
        {
            while (!Shutdown.IsCancellationRequested)
            {
                if (Console.KeyAvailable && Console.ReadKey(true).Key is ConsoleKey.Q or ConsoleKey.Escape) return;
                await Task.Delay(350, Shutdown.Token);
                try
                {
                    var next = await robot.GetSensorsAsync(Shutdown.Token);
                    if (current.Top.HasSample && next.Top.HasSample &&
                        (current.Top.Front != next.Top.Front || current.Top.Middle != next.Top.Middle || current.Top.Back != next.Top.Back))
                        ++changes;
                    context.UpdateTarget(new Rows(BuildSensorsView(next, current, changes),
                        new Markup("[grey]* changed since last reading. Press Q or Escape to exit.[/]")));
                    current = next;
                }
                catch (RobotException ex)
                {
                    context.UpdateTarget(new Markup($"[red]Sensor read failed: {Markup.Escape(ex.Message)}[/]\n" +
                        "[grey]Retrying. Press Q or Escape to exit.[/]"));
                }
                context.Refresh();
            }
        });
    }

    private static async Task DemosAsync(StackChanClient robot)
    {
        string selected = Pick("Sequential demos", "Introduction", "Expression sequence", Back);
        if (selected == Back) return;
        AnsiConsole.MarkupLine("[yellow]Press S for STOP while waiting. Ctrl+C cancels.[/]");
        async Task<bool> Stop(CancellationToken token)
        {
            if (Console.IsInputRedirected || !StopPressed()) return false;
            AnsiConsole.MarkupLine(Markup.Escape(await RequestStopAsync(robot, token)));
            return true;
        }
        void Progress(string message) => AnsiConsole.MarkupLine(Markup.Escape(message));
        if (selected == "Introduction")
            await SequenceDemos.IntroductionAsync(robot, Progress, Stop, Shutdown.Token);
        else
            await SequenceDemos.ExpressionsAsync(robot, Progress, Stop, Shutdown.Token);
        Message("Sequence finished.");
    }

    // Translate the chosen menu label into a typed action understood by the client.
    private static async Task ActionsAsync(StackChanClient robot)
    {
        string selected = Pick("Expression", "Smile", "Wink", "Neutral", "Look Up", "Sleep", "Wake",
            "Warning", "Important", "Important Notification", Back);
        RobotAction? action = selected switch
        {
            "Smile" => RobotAction.Smile,
            "Wink" => RobotAction.Wink,
            "Neutral" => RobotAction.Neutral,
            "Look Up" => RobotAction.LookUp,
            "Sleep" => RobotAction.Sleep,
            "Wake" => RobotAction.Wake,
            "Warning" => RobotAction.Warning,
            "Important" => RobotAction.Important,
            "Important Notification" => RobotAction.ImportantNotification,
            _ => null
        };
        if (action is not null) await PerformAsync(robot, action.Value);
    }

    // The client sends the HTTP request and validates its correlated receipt.
    private static async Task PerformAsync(StackChanClient robot, RobotAction action)
    {
        var result = await robot.PerformAsync(action, cancellationToken: Shutdown.Token);
        var receipt = result.Value.Command!;
        Message($"Command: [cyan]{Markup.Escape(receipt.State)}[/] (HTTP {(int)result.StatusCode})" +
            (receipt.Error is null ? "" : $"\n[red]{Markup.Escape(receipt.Error)}[/]"));
    }

    private static async Task SoundsAsync(StackChanClient robot)
    {
        string selected = Pick("Sounds", "Built-in sound", "Play WAV from microSD", Back);
        if (selected == "Built-in sound") await PerformAsync(robot, RobotAction.Sound);
        if (selected != "Play WAV from microSD") return;
        string file;
        try
        {
            AudioFiles files = await robot.GetAudioFilesAsync(Shutdown.Token);
            if (files.Files.Length == 0)
            {
                Message(files.Truncated ? "No WAV files found within the listing limit." :
                    "No WAV files found in the robot's /audio directory.");
                return;
            }
            if (files.Truncated) AnsiConsole.MarkupLine("[yellow]Listing is limited; more files may exist.[/]");
            file = Pick("microSD WAV file", files.Files.Append(Back).ToArray());
            if (file == Back) return;
        }
        catch (RobotException ex) when (ex.StatusCode is System.Net.HttpStatusCode.NotFound or System.Net.HttpStatusCode.MethodNotAllowed)
        {
            AnsiConsole.MarkupLine("[yellow]File listing unavailable on this firmware.[/]");
            if (Pick("Use manual filename?", "Manual filename", Back) == Back) return;
            file = AnsiConsole.Ask<string>("microSD WAV filename:");
        }
        var request = await robot.PlayWavAsync(file, Shutdown.Token);
        AnsiConsole.MarkupLine("[cyan]Accepted: queued[/] (playback not yet complete)");
        string last = "";
        var progress = new InlineProgress<AudioState>(state =>
        {
            if (state.State != last)
            {
                AnsiConsole.MarkupLine($"Audio: [cyan]{Markup.Escape(state.State)}[/]");
                last = state.State;
            }
        });
        var final = await robot.WaitForAudioAsync(request.Value, TimeSpan.FromSeconds(35), progress, Shutdown.Token);
        Message(final.State == "completed" ? "Playback completed according to firmware." :
            $"[yellow]Audio {Markup.Escape(final.State)}: {Markup.Escape(final.Error ?? "no error code")}[/]");
    }

    internal static readonly string[] BspMotionChoices =
    [
        "Left", "Right", "Up", "Down", "Home", "Nod Yes", "Shake No", "Dance",
        "Grumpy Shake", "Move X", "Move Y", "Move X/Y", "Refresh", "STOP", Back
    ];

    internal static BspMotionAction? BspMotionSelection(string selected) => selected switch
    {
        "Left" => BspMotionAction.Left, "Right" => BspMotionAction.Right,
        "Up" => BspMotionAction.Up, "Down" => BspMotionAction.Down,
        "Home" => BspMotionAction.Home, "Nod Yes" => BspMotionAction.NodYes,
        "Shake No" => BspMotionAction.ShakeNo, "Dance" => BspMotionAction.Dance,
        "Grumpy Shake" => BspMotionAction.GrumpyShake, "Move X" => BspMotionAction.MoveX,
        "Move Y" => BspMotionAction.MoveY, "Move X/Y" => BspMotionAction.MoveXY,
        _ => null
    };

    internal static int? ReadDirectionAmount(BspMotionAction action, Func<ConsoleKeyInfo> readKey, Action<string> write)
    {
        const int defaultAmount = 100;
        int max = action is BspMotionAction.Left or BspMotionAction.Right ? 2560 : 900;
        string input = "";
        write($"Relative movement (1-{max} units, default {defaultAmount}; Esc cancels): ");
        while (true)
        {
            ConsoleKeyInfo key = readKey();
            if (key.Key == ConsoleKey.Escape) { write("\n"); return null; }
            if (key.Key == ConsoleKey.Enter)
            {
                if (input.Length == 0) { write("\n"); return defaultAmount; }
                if (int.TryParse(input, out int amount) && amount >= 1 && amount <= max)
                {
                    write("\n");
                    return amount;
                }
                input = "";
                write($"\nEnter a whole number from 1 to {max}, or Esc to cancel: ");
            }
            else if (key.Key == ConsoleKey.Backspace && input.Length > 0)
            {
                input = input[..^1];
                write("\b \b");
            }
            else if (key.KeyChar is >= '0' and <= '9' && input.Length < 4)
            {
                input += key.KeyChar;
                write(key.KeyChar.ToString());
            }
        }
    }

    internal static async Task<BspMotionState?> ObserveBspMotionAsync(StackChanClient robot, BspMotionRequest request,
        Func<CancellationToken, Task<bool>> stopRequested, Action<BspMotionState> report, CancellationToken cancellationToken)
    {
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        deadline.CancelAfter(TimeSpan.FromSeconds(35));
        string? last = null;
        try
        {
            while (true)
            {
                if (await stopRequested(deadline.Token)) return null;
                BspMotionState state = await robot.GetBspMotionAsync(request, deadline.Token);
                if (state.State != last) { report(state); last = state.State; }
                if (state.State is "complete" or "stopped" or "failed" or "rejected" or "uncertain")
                    return state;
                await Task.Delay(250, deadline.Token);
            }
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
        {
            throw new RobotException("BSP observation timed out; outcome is unknown. Do not retry.",
                code: "motion_uncertain", outcomeUncertain: true);
        }
        catch (RobotException ex) when (!ex.OutcomeUncertain)
        {
            throw new RobotException($"BSP observation failed: {ex.Message} Outcome is unknown. Do not retry.",
                code: "motion_uncertain", outcomeUncertain: true, inner: ex);
        }
    }

    private static async Task<BspMotionState?> RunBspMotionAsync(StackChanClient robot, BspMotionAction action,
        int? value = null, int? x = null, int? y = null)
    {
        var request = await robot.StartBspMotionAsync(action, value, x, y, cancellationToken: Shutdown.Token);
        AnsiConsole.MarkupLine($"BSP request #{request.Value.RequestId} accepted; awaiting dispatch. Press S for STOP.");
        return await ObserveBspMotionAsync(robot, request.Value, async token =>
        {
            if (Console.IsInputRedirected || !StopPressed()) return false;
            AnsiConsole.MarkupLine(Markup.Escape(await RequestStopAsync(robot, token)));
            return true;
        }, state => AnsiConsole.MarkupLine(
            $"BSP {Markup.Escape(state.Action)}: {Markup.Escape(state.State)}" +
            (state.Error is null ? "" : $" / [red]{Markup.Escape(state.Error)}[/]") +
            " (physical result unverified)"), Shutdown.Token);
    }

    private static async Task MotionAsync(StackChanClient robot)
    {
        string? notice = null;
        while (!Shutdown.IsCancellationRequested)
        {
            var checkedMotion = await robot.GetMotionAsync(Shutdown.Token);
            var bsp = await robot.GetBspMotionAsync(Shutdown.Token);
            AnsiConsole.MarkupLine($"Checked motion: {Markup.Escape(checkedMotion.Motion?.State ?? "unknown")} " +
                $"{Markup.Escape(checkedMotion.Motion?.Error ?? "")}");
            AnsiConsole.MarkupLine($"BSP experimental admission: {(bsp.AdmissionReady ? "ready to request" : "unavailable or busy")}. " +
                "Home, UART and other checks still apply.");
            AnsiConsole.MarkupLine($"BSP #{bsp.RequestId}: {Markup.Escape(bsp.Action)} / {Markup.Escape(bsp.State)}" +
                (bsp.Error is null ? "" : $" / [red]{Markup.Escape(bsp.Error)}[/]") +
                " (physical result unverified)");
            if (notice is not null) AnsiConsole.MarkupLine(Markup.Escape(notice));
            string selected = Pick("Explicit BSP motion (STOP remains available)", BspMotionChoices);
            notice = null;
            if (selected == Back) return;
            if (selected == "Refresh") continue;
            if (selected == "STOP")
            {
                notice = await RequestStopAsync(robot, Shutdown.Token);
                continue;
            }
            BspMotionAction action = BspMotionSelection(selected) ??
                throw new InvalidOperationException("Unknown motion action.");
            if (!bsp.AdmissionReady)
            {
                notice = "BSP motion is busy or unavailable; no request was sent.";
                continue;
            }
            bool direction = action is BspMotionAction.Left or BspMotionAction.Right or BspMotionAction.Up or BspMotionAction.Down;
            int? value = null;
            if (direction)
            {
                if (bsp.DirectionReferenceReady is null)
                {
                    notice = "This firmware does not report directional readiness or support adjustable steps; update firmware before using directions.";
                    continue;
                }
                if (Console.IsInputRedirected)
                {
                    notice = "Directional input requires an interactive console.";
                    continue;
                }
                value = ReadDirectionAmount(action, () => Console.ReadKey(true), Console.Write);
                if (value is null) { notice = "Direction canceled; no request was sent."; continue; }
                if (bsp.DirectionReferenceReady == false)
                {
                    if (Pick("Home must complete before directions. Run Home?", "Run Home", "Cancel") != "Run Home")
                    {
                        notice = "Direction canceled; Home is required.";
                        continue;
                    }
                    var home = await RunBspMotionAsync(robot, BspMotionAction.Home);
                    if (home is null || home.State != "complete" || home.DirectionReferenceReady != true)
                    {
                        notice = "Home did not establish the directional reference; no direction was sent.";
                        continue;
                    }
                }
            }
            else if (action is BspMotionAction.MoveX or BspMotionAction.MoveY)
                value = AnsiConsole.Ask<int>("Offset (tenths of a degree):");
            int? x = action == BspMotionAction.MoveXY ? AnsiConsole.Ask<int>("X offset (tenths of a degree):") : null;
            int? y = action == BspMotionAction.MoveXY ? AnsiConsole.Ask<int>("Y offset (tenths of a degree):") : null;
            var result = await RunBspMotionAsync(robot, action, value, x, y);
            notice = result is null ? "STOP requested; motion outcome is not verified." :
                $"BSP {result.Action}: {result.State}" + (result.Error is null ? "" : $" / {result.Error}") +
                " (physical result unverified).";
        }
    }

    private static async Task ImagesAsync(StackChanClient robot)
    {
        bool refresh = false;
        while (true)
        {
            bool cached = ImageCache.Current is not null && !refresh;
            ImageFileList files;
            try
            {
                files = await ImageCache.GetAsync(() => LoadImagesAsync(robot), refresh);
            }
            catch (RobotException ex) when (refresh && ImageCache.Current is not null)
            {
                if (ex.Code is "unreachable" or "timeout") connection = "Disconnected";
                Message($"[red]Refresh failed: {Markup.Escape(ex.Message)}[/]\nCached list preserved.");
                refresh = false;
                continue;
            }
            catch (HttpRequestException) when (refresh && ImageCache.Current is not null)
            {
                connection = "Disconnected";
                Message("[red]Refresh failed: connection lost.[/]\nCached list preserved.");
                refresh = false;
                continue;
            }
            refresh = false;
            Header();
            if (files.Files.Length == 0)
                AnsiConsole.MarkupLine(files.Truncated ? "[yellow]No JPGs within listing limit.[/]" :
                    "[grey]No JPGs in /images/.[/]");
            if (files.Truncated) AnsiConsole.MarkupLine("[yellow]Listing limited; more files may exist.[/]");
            string title = cached ? $"Images ({files.Files.Length} cached)" : "Images from microSD";
            string file = AnsiConsole.Prompt(new SelectionPrompt<string>()
                .Title($"[purple]{Markup.Escape(title)}[/]").HighlightStyle(new Style(Color.Cyan))
                .PageSize(18).AddChoices(files.Files.Append(RefreshImages).Append(Back)));
            if (file == Back) return;
            if (file == RefreshImages) { refresh = true; continue; }
            try { await ShowImageAsync(robot, file); }
            catch (RobotException ex) when (ex.Code == "image_not_found")
            {
                Message("[yellow]Image not found. Choose Refresh (F).[/]");
            }
        }
    }

    private static Task<ImageFileList> LoadImagesAsync(StackChanClient robot) =>
        LoadImagesWithStatusAsync(AnsiConsole.Console, robot.GetImagesAsync, StopPressed,
            token => RequestStopAsync(robot, token), Shutdown.Token);

    internal static async Task<ImageFileList> LoadImagesWithStatusAsync(
        IAnsiConsole console, Func<CancellationToken, Task<ImageFileList>> load,
        Func<bool> stopPressed, Func<CancellationToken, Task<string>> stop, CancellationToken cancellationToken)
    {
        using var listing = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        Task<string>? pendingStop = null;
        string? stopResult = null;
        try
        {
            return await console.Status().Spinner(Spinner.Known.Dots)
                .StartAsync("Reading images from microSD... [grey](S: STOP)[/]", async context =>
            {
                var pending = load(listing.Token);
                while (!pending.IsCompleted)
                {
                    cancellationToken.ThrowIfCancellationRequested();
                    if (pendingStop is null && stopPressed())
                    {
                        context.Status("Reading images... [yellow]STOP requested[/]");
                        pendingStop = stop(cancellationToken);
                    }
                    if (pendingStop is { IsCompleted: true })
                    {
                        stopResult = await pendingStop;
                        pendingStop = null;
                        context.Status($"Reading images... [yellow]{Markup.Escape(stopResult)}[/]");
                    }
                    Task delay = Task.Delay(250, cancellationToken);
                    if (pendingStop is null) await Task.WhenAny(pending, delay);
                    else await Task.WhenAny(pending, pendingStop, delay);
                }
                return await pending;
            });
        }
        finally
        {
            listing.Cancel();
            if (pendingStop is not null) stopResult = await pendingStop;
            if (stopResult is not null) console.MarkupLine(Markup.Escape(stopResult));
        }
    }

    private static async Task ShowImageAsync(StackChanClient robot, string file)
    {
        var request = await robot.ShowImageAsync(file, Shutdown.Token);
        ImageCache.ObserveBoot(request.Value.BootId);
        AnsiConsole.MarkupLine("[cyan]Accepted: queued[/] (not yet displayed)");
        AnsiConsole.MarkupLine("[yellow]Press S for STOP while waiting.[/]");
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(Shutdown.Token);
        deadline.CancelAfter(TimeSpan.FromSeconds(35));
        string last = "queued";
        try
        {
            while (true)
            {
                await StopIfRequestedAsync(robot);
                ImageState state = await robot.GetImageStateAsync(request.Value, deadline.Token);
                if (state.State != last)
                {
                    AnsiConsole.MarkupLine($"Image: [cyan]{Markup.Escape(state.State)}[/]");
                    last = state.State;
                }
                if (state.State is "completed" or "failed" or "canceled")
                {
                    Message(state.State == "completed" ? "Image presentation completed according to firmware." :
                        $"[yellow]Image {Markup.Escape(state.State)}: {Markup.Escape(state.Error ?? "no error code")}[/]" +
                        (state.Error == "image_not_found" ? "\nChoose Refresh (F) to update the list." : ""));
                    return;
                }
                await Task.Delay(250, deadline.Token);
            }
        }
        catch (OperationCanceledException) when (!Shutdown.IsCancellationRequested)
        {
            throw new RobotException("Image observation timed out; display outcome is unknown.", code: "image_uncertain");
        }
    }

    private static bool StopPressed() => Console.KeyAvailable && Console.ReadKey(true).Key == ConsoleKey.S;

    private static async Task<string> RequestStopAsync(StackChanClient robot, CancellationToken cancellationToken)
    {
        var stop = await robot.StopAsync(cancellationToken);
        ImageCache.ObserveBoot(stop.Value.Receipt.BootId);
        return $"STOP: {stop.Value.Receipt.Outcome} (physical stopping unverified).";
    }

    private static async Task StopIfRequestedAsync(StackChanClient robot)
    {
        if (StopPressed()) AnsiConsole.MarkupLine(Markup.Escape(await RequestStopAsync(robot, Shutdown.Token)));
    }

    private static async Task RgbAsync(StackChanClient robot)
    {
        string choice = Pick("RGB color",
            "Purple", "Cyan", "Green", "Red", "Blue", "White", "Custom RGB", Back);
        if (choice == Back) return;
        string color = choice switch
        {
            "Purple" => "#800080",
            "Cyan" => "#00FFFF",
            "Green" => "#00FF00",
            "Red" => "#FF0000",
            "Blue" => "#0000FF",
            "White" => "#FFFFFF",
            _ => AnsiConsole.Prompt(new TextPrompt<string>("Custom #RRGGBB:")
                .Validate(value => StackChanClient.IsValidRgb(value)
                    ? ValidationResult.Success()
                    : ValidationResult.Error("[red]Use #RRGGBB format.[/]")))
        };
        int duration = AnsiConsole.Prompt(new TextPrompt<int>("Duration in milliseconds:")
            .DefaultValue(5000)
            .Validate(value => value is >= 100 and <= 30000
                ? ValidationResult.Success()
                : ValidationResult.Error("[red]Enter 100-30000 ms.[/]")));
        var request = await robot.SetRgbAsync(color, duration, Shutdown.Token);
        AnsiConsole.MarkupLine($"[cyan]RGB accepted[/]: {Markup.Escape(color)} for {duration} ms");
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(Shutdown.Token);
        deadline.CancelAfter(TimeSpan.FromSeconds(35));
        string last = "queued";
        while (true)
        {
            await StopIfRequestedAsync(robot);
            RgbState state = await robot.GetRgbStateAsync(request.Value, deadline.Token);
            if (state.State != last)
            {
                AnsiConsole.MarkupLine($"RGB: [cyan]{Markup.Escape(state.State)}[/]");
                last = state.State;
            }
            if (state.State is "completed" or "failed" or "canceled")
            {
                Message(state.State == "completed" ? "RGB effect completed according to firmware." :
                    $"[yellow]RGB {Markup.Escape(state.State)}: {Markup.Escape(state.Error ?? "no error code")}[/]");
                return;
            }
            await Task.Delay(250, deadline.Token);
        }
    }

    private static async Task TextAsync(StackChanClient robot)
    {
        string text = AnsiConsole.Prompt(new TextPrompt<string>("Temporary text:")
            .Validate(value =>
            {
                if (string.IsNullOrWhiteSpace(value) || value.Length > 64 ||
                    System.Text.Encoding.UTF8.GetByteCount(value) > 128 ||
                    value.Any(char.IsControl))
                    return ValidationResult.Error("[red]Enter 1-64 characters and at most 128 UTF-8 bytes.[/] ");
                return ValidationResult.Success();
            }));
        var request = await robot.ShowTextAsync(text, Shutdown.Token);
        AnsiConsole.MarkupLine("[cyan]Accepted: queued[/] (visible for five seconds)");
        AnsiConsole.MarkupLine("[yellow]Press S for STOP while waiting.[/]");
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(Shutdown.Token);
        deadline.CancelAfter(TimeSpan.FromSeconds(10));
        string last = "queued";
        try
        {
            TextState final;
            while (true)
            {
                await StopIfRequestedAsync(robot);
                final = await robot.GetTextStateAsync(request.Value, deadline.Token);
                if (final.State != last)
                {
                    AnsiConsole.MarkupLine($"Text: [cyan]{Markup.Escape(final.State)}[/]");
                    last = final.State;
                }
                if (final.State is "completed" or "failed" or "canceled") break;
                await Task.Delay(250, deadline.Token);
            }
            Message(final.State == "completed" ? "Text presentation completed; avatar restoration requested." :
                $"[yellow]Text {Markup.Escape(final.State)}: {Markup.Escape(final.Error ?? "no error code")}[/]");
        }
        catch (OperationCanceledException) when (!Shutdown.IsCancellationRequested)
        {
            throw new RobotException("Text observation timed out; display outcome is unknown.", code: "text_uncertain");
        }
    }

    private static async Task ScreenAsync(StackChanClient robot)
    {
        string selected = Pick("Screen & brightness", "Current screen", "Navigate", "Touch control",
            "Show brightness", "Set brightness", Back);
        switch (selected)
        {
            case "Current screen":
                var screen = await robot.GetScreenAsync(Shutdown.Token);
                Message($"Screen: [cyan]{Markup.Escape(screen.Title)}[/] ({Markup.Escape(screen.Id)})");
                break;
            case "Navigate":
                var screens = await robot.GetScreensAsync(Shutdown.Token);
                var ids = screens.Screens.Where(s => StackChanClient.IsSafeScreen(s.Id)).Select(s => s.Id).Append(Back).ToArray();
                string id = Pick("Screen", ids);
                if (id != Back)
                {
                    var result = await robot.NavigateScreenAsync(id, Shutdown.Token);
                    Message($"Navigation {(result.Value.Accepted ? "queued" : "not accepted")} (HTTP {(int)result.StatusCode}).");
                }
                break;
            case "Touch control":
                var current = await robot.GetScreenAsync(Shutdown.Token);
                var controls = current.Elements.Where(e => StackChanClient.IsSafeControl(current.Id, e))
                    .Select(e => e.Id).Append(Back).ToArray();
                string control = Pick("Current screen control", controls);
                if (control != Back)
                {
                    var result = await robot.TouchAsync(current.Id, control, Shutdown.Token);
                    Message($"Touch {(result.Value.Accepted ? "queued" : "not accepted")} (HTTP {(int)result.StatusCode}).");
                }
                break;
            case "Show brightness":
                var brightness = await robot.GetBrightnessAsync(Shutdown.Token);
                Message($"Brightness: {brightness.Brightness}  Pending: {brightness.Pending}" +
                    (brightness.Error is null ? "" : $"\n[red]{Markup.Escape(brightness.Error)}[/]"));
                break;
            case "Set brightness":
                var currentBrightness = await robot.GetBrightnessAsync(Shutdown.Token);
                AnsiConsole.MarkupLine($"Current: [cyan]{currentBrightness.Brightness}[/]  Default: {currentBrightness.Default}");
                AnsiConsole.MarkupLine($"Range: {currentBrightness.Minimum}-{currentBrightness.Maximum}" +
                    $"  Pending: {currentBrightness.Pending}");
                int value = AnsiConsole.Prompt(new TextPrompt<int>("New brightness:")
                    .Validate(number => number >= currentBrightness.Minimum && number <= currentBrightness.Maximum
                        ? ValidationResult.Success()
                        : ValidationResult.Error($"[red]Enter {currentBrightness.Minimum}-{currentBrightness.Maximum}.[/]")));
                var reply = await robot.SetBrightnessAsync(value, Shutdown.Token);
                Message($"Brightness: {(reply.Value.Pending ? "queued" : "not pending")}, current {reply.Value.Brightness}.");
                break;
        }
    }

    private static async Task StatusAsync(StackChanClient robot)
    {
        var status = await robot.GetStatusAsync(Shutdown.Token);
        ImageCache.ObserveBoot(status.BootId);
        var network = await robot.GetNetworkAsync(Shutdown.Token);
        Message($"Firmware: {Markup.Escape(status.FirmwareVersion ?? "unknown")}\n" +
            $"Network: {Markup.Escape(network.State)}\nMotion: {Markup.Escape(status.Motion?.State ?? "unknown")}" +
            $"\nCapabilities: {Markup.Escape(string.Join(", ", status.Capabilities))}");
    }

    internal sealed class ImageListCache
    {
        private uint? bootId;
        private int generation;
        public ImageFileList? Current { get; private set; }

        public void Invalidate()
        {
            Current = null;
            generation++;
        }

        public void ObserveBoot(uint boot)
        {
            if (bootId is not null && bootId != boot) Invalidate();
            bootId = boot;
        }

        public async Task<ImageFileList> GetAsync(Func<Task<ImageFileList>> load, bool refresh = false)
        {
            if (!refresh && Current is not null) return Current;
            int startedAt = generation;
            ImageFileList result = await load();
            if (startedAt != generation)
                throw new RobotException("Robot identity changed during image listing.", code: "image_list_uncertain");
            Current = result;
            return result;
        }
    }

    private sealed class InlineProgress<T>(Action<T> report) : IProgress<T>
    {
        public void Report(T value) => report(value);
    }
}
