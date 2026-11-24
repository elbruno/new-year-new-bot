using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace StackChan.RobotClient;

/// <summary>Typed HTTP access to a StackChan on a trusted, isolated network.</summary>
public sealed class StackChanClient
{
    private static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web)
    {
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull
    };
    private readonly HttpClient http;
    private readonly Uri endpoint;
    private readonly SemaphoreSlim commandGate = new(1, 1);
    private static readonly HashSet<string> BlockedScreens =
        ["setup-key", "shutdown", "notification", "notification-detail", "notification-actions", "nfc", "nfc-result"];
    private static readonly HashSet<string> BlockedElements =
    [
        "btn-stop", "btn-shutdown", "btn-shut-down", "btn-cancel", "btn-reset", "btn-reset-robot",
        "btn-reboot", "btn-power", "btn-power-off", "btn-arm", "btn-release-torque", "btn-setup",
        "btn-show-setup-key", "btn-end-setup", "btn-enable", "btn-lock", "btn-home", "btn-notify",
        "btn-message", "btn-open", "btn-discard", "btn-acknowledge", "btn-review-in-companion",
        "btn-nfc", "btn-read-phone", "btn-read-result"
    ];

    // Accept only a private IPv4 HTTP origin, not a path, proxy URL or public host.
    public StackChanClient(HttpClient httpClient, string address)
    {
        ArgumentNullException.ThrowIfNull(httpClient);
        if (!Uri.TryCreate(address, UriKind.Absolute, out var uri) || uri.Scheme != Uri.UriSchemeHttp ||
            uri.UserInfo.Length != 0 || uri.AbsolutePath != "/" || uri.Query.Length != 0 ||
            uri.Fragment.Length != 0 || !IPAddress.TryParse(uri.Host, out var ip) ||
            ip.AddressFamily != AddressFamily.InterNetwork || ip.ToString() != uri.Host ||
            !IsPrivate(ip))
            throw new ArgumentException("The robot endpoint must be an HTTP origin with a private IPv4 address.", nameof(address));
        http = httpClient;
        endpoint = uri;
    }

    private static bool IsPrivate(IPAddress address)
    {
        byte[] b = address.GetAddressBytes();
        return b[0] == 10 || b[0] == 172 && b[1] is >= 16 and <= 31 || b[0] == 192 && b[1] == 168;
    }

    // Version identifies the firmware; status includes the current boot and capabilities.
    public async Task<RobotVersion> GetVersionAsync(CancellationToken cancellationToken = default) =>
        (await SendAsync<RobotVersion>(HttpMethod.Get, "/api/version", null, cancellationToken)).Value;
    public async Task<RobotStatus> GetStatusAsync(CancellationToken cancellationToken = default)
    {
        var status = (await SendAsync<RobotStatus>(HttpMethod.Get, "/api/status", null, cancellationToken)).Value;
        if (status.BootId == 0 || status.Capabilities is null)
            throw new RobotException("Invalid robot status.", code: "invalid_response");
        return status;
    }
    public async Task<bool> CheckConnectivityAsync(CancellationToken cancellationToken = default)
    {
        try { await GetStatusAsync(cancellationToken); return true; }
        catch (RobotException) { return false; }
    }
    public async Task<string[]> GetCapabilitiesAsync(CancellationToken cancellationToken = default) =>
        (await GetStatusAsync(cancellationToken)).Capabilities;
    public async Task<RobotNetwork> GetNetworkAsync(CancellationToken cancellationToken = default) =>
        (await SendAsync<RobotNetwork>(HttpMethod.Get, "/api/network", null, cancellationToken)).Value;
    public async Task<SensorDiagnostics> GetSensorsAsync(CancellationToken cancellationToken = default)
    {
        var s = (await SendAsync<SensorDiagnostics>(HttpMethod.Get, "/api/sensors", null, cancellationToken)).Value;
        static bool ValidVector(ImuVector? v, ImuDiagnostics imu) =>
            v is not null &&
            (v.HasSample
                ? v.AgeMs is not null && v.X is double x && double.IsFinite(x) &&
                    v.Y is double y && double.IsFinite(y) && v.Z is double z && double.IsFinite(z)
                : v.AgeMs is null && v.X is null && v.Y is null && v.Z is null) &&
            (!v.Fresh || v.HasSample && imu.Available && imu.Error is null);
        if (s.Top is null || s.Light is null || s.Rgb is null || s.Petting is null ||
            s.Imu is { } imu && (!ValidVector(imu.AccelerationG, imu) || !ValidVector(imu.GyroscopeDps, imu)) ||
            s.Top.HasSample != (s.Top.AgeMs is not null && s.Top.Front is >= 0 and <= 3 &&
                s.Top.Middle is >= 0 and <= 3 && s.Top.Back is >= 0 and <= 3) ||
            !s.Top.HasSample && (s.Top.AgeMs is not null || s.Top.Front is not null || s.Top.Middle is not null || s.Top.Back is not null) ||
            s.Top.Fresh && (!s.Top.HasSample || !s.Top.Available || s.Top.Error is not null) ||
            s.Light.HasSample != (s.Light.AgeMs is not null && s.Light.Proximity is >= 0 and <= 2047 &&
                s.Light.Als0 is >= 0 and <= 65535 && s.Light.Als1 is >= 0 and <= 65535) ||
            !s.Light.HasSample && (s.Light.AgeMs is not null || s.Light.Proximity is not null || s.Light.Als0 is not null || s.Light.Als1 is not null) ||
            s.Light.Fresh && (!s.Light.HasSample || !s.Light.Available || s.Light.Error is not null) ||
            s.Rgb.Active != (s.Rgb.DurationMs is not null) ||
            s.Petting.Eligible != (s.Petting.IneligibleReason is null) ||
            string.IsNullOrEmpty(s.Petting.SoundState) || s.Petting.Stage is < 0 or > 3 ||
            s.Petting.TouchCount is < 0 or > 2 || s.Petting.ReleaseCount is < 0 or > 2)
            throw new RobotException("Invalid sensor response.", code: "invalid_response");
        return s;
    }
    public async Task<RobotStatus> GetMotionAsync(CancellationToken cancellationToken = default) =>
        (await SendAsync<RobotStatus>(HttpMethod.Get, "/api/motion", null, cancellationToken)).Value;

    public async Task<BspMotionState> GetBspMotionAsync(CancellationToken cancellationToken = default)
    {
        var state = (await SendAsync<BspMotionState>(HttpMethod.Get, "/api/debug/motion-test", null, cancellationToken)).Value;
        if (state.BootId == 0 || state.Action is null || state.PhysicalResult != "unverified" ||
            state.State is not ("idle" or "preparing" or "running" or "complete" or "stopped" or "failed" or "rejected" or "uncertain"))
            throw new RobotException("Invalid BSP motion status.", code: "invalid_response");
        return state;
    }

    public async Task<BspMotionState> GetBspMotionAsync(BspMotionRequest request,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(request);
        var state = await GetBspMotionAsync(cancellationToken);
        if (state.BootId != request.BootId || state.RequestId != request.RequestId || state.Action != request.Action)
            throw new RobotException("BSP motion identity changed; outcome is unknown. Do not retry.",
                code: "motion_uncertain", outcomeUncertain: true);
        return state;
    }

    public async Task<RobotResult<BspMotionRequest>> StartBspMotionAsync(BspMotionAction action,
        int? value = null, int? x = null, int? y = null, int? speed = null,
        CancellationToken cancellationToken = default)
    {
        string name = action switch
        {
            BspMotionAction.Left => "head_left", BspMotionAction.Right => "head_right",
            BspMotionAction.Up => "head_up", BspMotionAction.Down => "head_down",
            BspMotionAction.Home => "home_go", BspMotionAction.MoveX => "move_x",
            BspMotionAction.MoveY => "move_y", BspMotionAction.MoveXY => "move_xy",
            BspMotionAction.NodYes => "nod_yes", BspMotionAction.ShakeNo => "shake_no",
            BspMotionAction.Dance => "dance", BspMotionAction.GrumpyShake => "grumpy_shake",
            _ => throw new ArgumentOutOfRangeException(nameof(action))
        };
        bool offset = action is BspMotionAction.MoveX or BspMotionAction.MoveY;
        bool direction = action is BspMotionAction.Left or BspMotionAction.Right or BspMotionAction.Up or BspMotionAction.Down;
        bool xy = action == BspMotionAction.MoveXY;
        bool supportsSpeed = offset || xy || action == BspMotionAction.Home;
        if ((!direction && offset != value.HasValue) ||
            direction && value.HasValue && (value < 1 ||
                value > (action is BspMotionAction.Left or BspMotionAction.Right ? 2560 : 900)) ||
            xy != (x.HasValue && y.HasValue) ||
            !xy && (x.HasValue || y.HasValue) ||
            action == BspMotionAction.MoveX && value is < -1280 or > 1280 ||
            action == BspMotionAction.MoveY && value is < 0 or > 900 ||
            x is < -1280 or > 1280 || y is < 0 or > 900 ||
            speed.HasValue && (!supportsSpeed || speed is < 0 or > 1000))
            throw new ArgumentException("Invalid BSP motion parameters.");
        var result = await SendAsync<BspMotionRequest>(HttpMethod.Post, "/api/debug/motion-test",
            new { action = name, value, x, y, speed }, cancellationToken);
        if (result.StatusCode != HttpStatusCode.Accepted || !result.Value.Accepted ||
            result.Value.BootId == 0 || result.Value.RequestId == 0 || result.Value.Action != name ||
            result.Value.State != "queued" || result.Value.PhysicalResult != "unverified")
            throw new RobotException("BSP motion receipt is invalid. Do not retry.", result.StatusCode,
                "motion_uncertain", outcomeUncertain: true);
        return result;
    }
    public async Task<ScreenList> GetScreensAsync(CancellationToken cancellationToken = default) =>
        (await SendAsync<ScreenList>(HttpMethod.Get, "/api/ui/screens", null, cancellationToken)).Value;
    public async Task<Screen> GetScreenAsync(CancellationToken cancellationToken = default) =>
        (await SendAsync<Screen>(HttpMethod.Get, "/api/ui/screen", null, cancellationToken)).Value;
    public static bool IsSafeScreen(string? id) => IsIdentifier(id) && !BlockedScreens.Contains(id!);
    public static bool IsSafeControl(string? screen, ScreenElement element) =>
        IsSafeScreen(screen) && IsIdentifier(element.Id) && !BlockedElements.Contains(element.Id) &&
        element.Enabled && !element.Motion && element.Type is "navigate" or "action" or "toggle" or "adjust";
    private static bool IsIdentifier(string? value) => value is { Length: > 0 and < 40 } &&
        char.IsAsciiLetterLower(value[0]) &&
        value.All(c => char.IsAsciiLetterLower(c) || char.IsAsciiDigit(c) || c == '-');

    public async Task<RobotResult<UiAcceptance>> NavigateScreenAsync(string id, CancellationToken cancellationToken = default)
    {
        if (!IsSafeScreen(id)) throw new RobotException("Screen is blocked.", code: "screen_blocked");
        var screens = await GetScreensAsync(cancellationToken);
        if (!screens.Screens.Any(screen => screen.Id == id))
            throw new RobotException("Screen is unavailable.", code: "unknown_screen");
        var result = await SendAsync<UiAcceptance>(HttpMethod.Post, "/api/ui/screen", new { id }, cancellationToken);
        return CheckUiAcceptance(result);
    }
    public async Task<RobotResult<UiAcceptance>> TouchAsync(string screen, string element, CancellationToken cancellationToken = default)
    {
        if (!IsSafeScreen(screen)) throw new RobotException("Screen is blocked.", code: "screen_blocked");
        Screen current = await GetScreenAsync(cancellationToken);
        ScreenElement? control = current.Elements.SingleOrDefault(item => item.Id == element);
        if (current.Id != screen || control is null || !IsSafeControl(screen, control))
            throw new RobotException("Control is unavailable or blocked.", code: "element_blocked");
        var result = await SendAsync<UiAcceptance>(HttpMethod.Post, "/api/ui/touch", new { screen, element }, cancellationToken);
        return CheckUiAcceptance(result);
    }
    private static RobotResult<UiAcceptance> CheckUiAcceptance(RobotResult<UiAcceptance> result) =>
        result.StatusCode == HttpStatusCode.Accepted && result.Value.Accepted ? result :
            throw new RobotException("UI response is invalid. Do not retry automatically.", result.StatusCode,
                "invalid_ui_receipt", outcomeUncertain: true);
    public async Task<DisplayBrightness> GetBrightnessAsync(CancellationToken cancellationToken = default) =>
        (await SendAsync<DisplayBrightness>(HttpMethod.Get, "/api/display/brightness", null, cancellationToken)).Value;
    public Task<RobotResult<DisplayBrightness>> SetBrightnessAsync(int brightness, CancellationToken cancellationToken = default)
    {
        if (brightness is < 32 or > 192) throw new ArgumentOutOfRangeException(nameof(brightness));
        return SendAsync<DisplayBrightness>(HttpMethod.Post, "/api/display/brightness", new { brightness }, cancellationToken);
    }

    // Serialize commands, bind each to the current boot and verify the returned receipt.
    public async Task<RobotResult<RobotStatus>> PerformAsync(RobotAction action, int durationMs = 1000,
        CancellationToken cancellationToken = default)
    {
        if (durationMs is < 100 or > 5000) throw new ArgumentOutOfRangeException(nameof(durationMs));
        (string name, string route, bool motion) = ActionInfo(action);
        await commandGate.WaitAsync(cancellationToken);
        try
        {
            RobotStatus status = await GetStatusAsync(cancellationToken);
            if (!status.Capabilities.Contains(name, StringComparer.Ordinal))
                throw new RobotException($"Action is unavailable: {status.Motion?.Error ?? name}.",
                    code: status.Motion?.Error ?? "unsupported_action");
            // The next command ID is scoped to the boot reported by this status snapshot.
            uint lastId = status.Command?.CommandId ?? 0;
            if (lastId == uint.MaxValue) throw new RobotException("Command IDs exhausted.", code: "command_ids_exhausted");
            uint id = lastId + 1;
            uint? revision = motion && status.Motion?.ConfigRevision is > 0 ? status.Motion.ConfigRevision : null;
            var result = await SendAsync<RobotStatus>(HttpMethod.Post, route,
                new { bootId = status.BootId, commandId = id, action = name, durationMs, configRevision = revision },
                cancellationToken);
            // A mismatched receipt leaves the outcome uncertain; never replay it automatically.
            var receipt = result.Value.Command;
            if (result.StatusCode is not (HttpStatusCode.OK or HttpStatusCode.Accepted) ||
                result.Value.BootId != status.BootId || receipt is null || receipt.CommandId != id ||
                receipt.Action != name || receipt.DurationMs != durationMs || receipt.ConfigRevision != revision)
                throw new RobotException("Command response could not be correlated. Do not retry.",
                    result.StatusCode, "uncorrelated_response", outcomeUncertain: true);
            return result;
        }
        finally { commandGate.Release(); }
    }

    public async Task<RobotResult<StopResult>> StopAsync(CancellationToken cancellationToken = default)
    {
        var status = await GetStatusAsync(cancellationToken);
        var result = await SendAsync<JsonElement>(HttpMethod.Post, "/api/stop", new { bootId = status.BootId }, cancellationToken);
        RobotStatus? returnedStatus = null;
        StopDiagnostic? diagnostic = null;
        StopReceipt? receipt;
        try
        {
            if (result.Value.ValueKind != JsonValueKind.Object) throw new JsonException("Invalid stop response.");
            if (result.Value.TryGetProperty("rail", out _))
            {
                diagnostic = result.Value.Deserialize<StopDiagnostic>(Json);
                receipt = diagnostic?.StopReceipt;
            }
            else
            {
                returnedStatus = result.Value.Deserialize<RobotStatus>(Json);
                receipt = returnedStatus?.StopReceipt;
            }
        }
        catch (JsonException ex)
        {
            throw new RobotException("Stop receipt is invalid. Do not retry automatically.", result.StatusCode,
                "stop_unconfirmed", outcomeUncertain: true, inner: ex);
        }
        string expected = result.StatusCode == HttpStatusCode.Accepted ? "pending" :
            diagnostic is not null ? "unconfirmed" : "confirmed";
        if (result.StatusCode is not (HttpStatusCode.OK or HttpStatusCode.Accepted) ||
            returnedStatus is not null && returnedStatus.BootId != status.BootId ||
            diagnostic is not null && !ValidStopDiagnostic(diagnostic, result.StatusCode) ||
            receipt is null || receipt.BootId != status.BootId || receipt.StopId == 0 || receipt.Outcome != expected)
            throw new RobotException("Stop receipt is unconfirmed. Do not retry automatically.", result.StatusCode,
                "stop_unconfirmed", outcomeUncertain: true);
        return new(result.StatusCode, new StopResult(receipt, returnedStatus, diagnostic));
    }

    private static bool ValidStopDiagnostic(StopDiagnostic diagnostic, HttpStatusCode code)
    {
        if (diagnostic.PhysicalStop != "unverified" || diagnostic.Rail.ValueKind != JsonValueKind.Object ||
            !diagnostic.Rail.TryGetProperty("state", out var state) || state.ValueKind != JsonValueKind.String)
            return false;
        string? rail = state.GetString();
        return code == HttpStatusCode.OK
            ? rail == "request_accepted" && diagnostic.Torque == "acknowledged"
            : rail is "pending" or "request_accepted" && diagnostic.Torque is "pending" or "acknowledged" &&
                !(rail == "request_accepted" && diagnostic.Torque == "acknowledged");
    }

    public async Task<RobotResult<AudioRequest>> PlayWavAsync(string file, CancellationToken cancellationToken = default)
    {
        var result = await SendAsync<AudioRequest>(HttpMethod.Post, "/api/debug/audio-test", new { file }, cancellationToken);
        if (result.StatusCode != HttpStatusCode.Accepted || !result.Value.Accepted ||
            result.Value.BootId == 0 || result.Value.RequestId == 0 || result.Value.State != "queued")
            throw new RobotException("Audio request identity is invalid. Do not retry.", result.StatusCode,
                "invalid_audio_receipt", outcomeUncertain: true);
        return result;
    }

    public async Task<AudioFiles> GetAudioFilesAsync(CancellationToken cancellationToken = default)
    {
        var result = (await SendAsync<AudioFiles>(HttpMethod.Get, "/api/debug/audio-files", null, cancellationToken)).Value;
        if (result.Files is null || result.Files.Length > 16 || result.Files.Any(name =>
                string.IsNullOrEmpty(name) || name.Length is < 5 or > 64 || name[0] == '.' ||
                !name.EndsWith(".wav", StringComparison.Ordinal) || name.Contains("..", StringComparison.Ordinal) ||
                !name.All(c => char.IsAsciiLetterOrDigit(c) || c is '_' or '-' or '.')))
            throw new RobotException("Invalid audio file list.", code: "invalid_response");
        return result;
    }

    public async Task<AudioState> GetAudioStateAsync(AudioRequest request, CancellationToken cancellationToken = default)
    {
        var state = (await SendAsync<AudioState>(HttpMethod.Get, "/api/debug/audio-test", null, cancellationToken)).Value;
        if (state.BootId != request.BootId || state.RequestId != request.RequestId ||
            state.State is not ("queued" or "loading" or "playing" or "canceling" or "completed" or "canceled" or "failed"))
            throw new RobotException("Audio identity or state changed; playback outcome is unknown.", code: "audio_uncertain");
        return state;
    }

    public async Task<AudioState> WaitForAudioAsync(AudioRequest request, TimeSpan timeout,
        IProgress<AudioState>? progress = null, CancellationToken cancellationToken = default)
    {
        if (timeout <= TimeSpan.Zero) throw new ArgumentOutOfRangeException(nameof(timeout));
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        deadline.CancelAfter(timeout);
        try
        {
            while (true)
            {
                var state = await GetAudioStateAsync(request, deadline.Token);
                progress?.Report(state);
                if (state.State is "completed" or "failed" or "canceled") return state;
                await Task.Delay(250, deadline.Token);
            }
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
        {
            throw new RobotException("Audio observation timed out; playback outcome is unknown.", code: "audio_uncertain");
        }
    }

    private static bool IsImageFilename(string? name) =>
        name is { Length: >= 5 and <= 64 } && name[0] != '.' &&
        name.EndsWith(".jpg", StringComparison.Ordinal) && !name.Contains("..", StringComparison.Ordinal) &&
        name.All(c => char.IsAsciiLetterOrDigit(c) || c is '_' or '-' or '.');

    public async Task<ImageFileList> GetImagesAsync(CancellationToken cancellationToken = default)
    {
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        deadline.CancelAfter(TimeSpan.FromSeconds(15));
        try
        {
            while (true)
            {
                try
                {
                    var result = (await SendAsync<ImageFileList>(HttpMethod.Get, "/api/display/images", null, deadline.Token)).Value;
                    if (result.Files is null || result.Files.Length > 16 || result.Files.Any(name => !IsImageFilename(name)))
                        throw new RobotException("Invalid image file list.", code: "invalid_response");
                    return result;
                }
                catch (RobotException ex) when (ex.StatusCode == HttpStatusCode.ServiceUnavailable && ex.Code == "image_list_pending")
                {
                    await Task.Delay(250, deadline.Token);
                }
            }
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
        {
            throw new RobotException("Image listing timed out.", code: "image_list_pending");
        }
    }

    public async Task<RobotResult<ImageRequest>> ShowImageAsync(string filename, CancellationToken cancellationToken = default)
    {
        if (!IsImageFilename(filename)) throw new ArgumentException("Invalid JPG filename.", nameof(filename));
        var result = await SendAsync<ImageRequest>(HttpMethod.Post, "/api/display/image", new { file = filename }, cancellationToken);
        if (result.StatusCode != HttpStatusCode.Accepted || !result.Value.Accepted ||
            result.Value.BootId == 0 || result.Value.RequestId == 0 || result.Value.State != "queued")
            throw new RobotException("Image request identity is invalid. Do not retry.", result.StatusCode,
                "invalid_image_receipt", outcomeUncertain: true);
        return result;
    }

    public async Task<ImageState> GetImageStateAsync(ImageRequest request, CancellationToken cancellationToken = default)
    {
        var state = (await SendAsync<ImageState>(HttpMethod.Get, "/api/display/image", null, cancellationToken)).Value;
        if (state.BootId != request.BootId || state.RequestId != request.RequestId ||
            state.State is not ("queued" or "loading" or "canceling" or "displaying" or "completed" or "canceled" or "failed"))
            throw new RobotException("Image identity or state changed; display outcome is unknown.", code: "image_uncertain");
        return state;
    }

    public async Task<RobotResult<TextRequest>> ShowTextAsync(string text, CancellationToken cancellationToken = default)
    {
        if (text is null || text.Length == 0 || text.Length > 64 ||
            Encoding.UTF8.GetByteCount(text) > 128 || text.Any(char.IsControl))
            throw new ArgumentException("Text must contain 1-64 characters and at most 128 UTF-8 bytes.", nameof(text));
        var result = await SendAsync<TextRequest>(HttpMethod.Post, "/api/display/text", new { text }, cancellationToken);
        if (result.StatusCode != HttpStatusCode.Accepted || !result.Value.Accepted ||
            result.Value.BootId == 0 || result.Value.RequestId == 0 || result.Value.State != "queued")
            throw new RobotException("Text request identity is invalid. Do not retry.", result.StatusCode,
                "invalid_text_receipt", outcomeUncertain: true);
        return result;
    }

    public async Task<TextState> GetTextStateAsync(TextRequest request, CancellationToken cancellationToken = default)
    {
        var state = (await SendAsync<TextState>(HttpMethod.Get, "/api/display/text", null, cancellationToken)).Value;
        if (state.BootId != request.BootId || state.RequestId != request.RequestId ||
            state.State is not ("queued" or "displaying" or "completed" or "canceled" or "failed"))
            throw new RobotException("Text identity or state changed; display outcome is unknown.", code: "text_uncertain");
        return state;
    }

    public async Task<TextState> WaitForTextAsync(TextRequest request, TimeSpan timeout,
        IProgress<TextState>? progress = null, CancellationToken cancellationToken = default)
    {
        if (timeout <= TimeSpan.Zero) throw new ArgumentOutOfRangeException(nameof(timeout));
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        deadline.CancelAfter(timeout);
        try
        {
            while (true)
            {
                var state = await GetTextStateAsync(request, deadline.Token);
                progress?.Report(state);
                if (state.State is "completed" or "failed" or "canceled") return state;
                await Task.Delay(250, deadline.Token);
            }
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
        {
            throw new RobotException("Text observation timed out; display outcome is unknown.", code: "text_uncertain");
        }
    }

    public static bool IsValidRgb(string? color) =>
        color is { Length: 7 } && color[0] == '#' &&
        color[1..].All(c => char.IsAsciiHexDigit(c));

    public async Task<RobotResult<RgbRequest>> SetRgbAsync(string color, int durationMs,
        CancellationToken cancellationToken = default)
    {
        if (!IsValidRgb(color))
            throw new ArgumentException("Color must use #RRGGBB format.", nameof(color));
        if (durationMs is < 100 or > 30000)
            throw new ArgumentOutOfRangeException(nameof(durationMs));
        var result = await SendAsync<RgbRequest>(HttpMethod.Post, "/api/display/rgb",
            new { color, durationMs }, cancellationToken);
        if (result.StatusCode != HttpStatusCode.Accepted || !result.Value.Accepted ||
            result.Value.BootId == 0 || result.Value.RequestId == 0 || result.Value.State != "queued")
            throw new RobotException("RGB request identity is invalid. Do not retry.", result.StatusCode,
                "invalid_rgb_receipt", outcomeUncertain: true);
        return result;
    }

    public async Task<RgbState> GetRgbStateAsync(RgbRequest request, CancellationToken cancellationToken = default)
    {
        var state = (await SendAsync<RgbState>(HttpMethod.Get, "/api/display/rgb", null, cancellationToken)).Value;
        if (state.BootId != request.BootId || state.RequestId != request.RequestId ||
            state.State is not ("queued" or "active" or "completed" or "canceled" or "failed"))
            throw new RobotException("RGB identity or state changed; effect outcome is unknown.", code: "rgb_uncertain");
        return state;
    }

    private static (string Name, string Route, bool Motion) ActionInfo(RobotAction action) => action switch
    {
        RobotAction.Neutral => ("neutral", "/api/actions", false),
        RobotAction.Wink => ("wink", "/api/actions", false),
        RobotAction.Smile => ("smile", "/api/actions", false),
        RobotAction.LookUp => ("look_up", "/api/actions", false),
        RobotAction.Sleep => ("sleep", "/api/actions", false),
        RobotAction.Wake => ("wake", "/api/actions", false),
        RobotAction.Warning => ("warning", "/api/actions", false),
        RobotAction.Important => ("important", "/api/actions", false),
        RobotAction.Sound => ("sound", "/api/actions", false),
        RobotAction.ImportantNotification => ("important_notification", "/api/routines", false),
        RobotAction.Nod => ("nod", "/api/motion", true),
        RobotAction.MoveLeft => ("move_left", "/api/motion", true),
        RobotAction.MoveRight => ("move_right", "/api/motion", true),
        RobotAction.MoveUp => ("move_up", "/api/motion", true),
        RobotAction.MoveDown => ("move_down", "/api/motion", true),
        RobotAction.Home => ("home", "/api/motion", true),
        _ => throw new ArgumentOutOfRangeException(nameof(action))
    };

    // Centralize HTTP and JSON handling; a failed POST may already have reached the robot.
    private async Task<RobotResult<T>> SendAsync<T>(HttpMethod method, string path, object? body, CancellationToken token)
    {
        bool post = method == HttpMethod.Post;
        using var request = new HttpRequestMessage(method, new Uri(endpoint, path));
        if (body is not null)
            request.Content = new StringContent(JsonSerializer.Serialize(body, Json), Encoding.UTF8, "application/json");
        try
        {
            using var response = await http.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, token);
            await using var stream = await response.Content.ReadAsStreamAsync(token);
            using var buffer = new MemoryStream();
            byte[] chunk = new byte[4096];
            int count;
            while ((count = await stream.ReadAsync(chunk, token)) != 0)
            {
                if (buffer.Length + count > 65536) throw new JsonException("Response too large.");
                buffer.Write(chunk, 0, count);
            }
            JsonDocument document;
            try { document = JsonDocument.Parse(buffer.ToArray()); }
            catch (JsonException) when (!response.IsSuccessStatusCode)
            {
                throw new RobotException($"Robot returned HTTP {(int)response.StatusCode}.",
                    response.StatusCode, response.StatusCode is HttpStatusCode.NotFound or HttpStatusCode.MethodNotAllowed
                        ? "endpoint_unavailable" : null, outcomeUncertain: post && (int)response.StatusCode >= 500);
            }
            using (document)
            {
                JsonElement root = document.RootElement;
                if (!response.IsSuccessStatusCode)
                {
                    string? code = root.ValueKind == JsonValueKind.Object && root.TryGetProperty("error", out var error) &&
                        error.ValueKind == JsonValueKind.String ? error.GetString() : null;
                    throw new RobotException($"Robot returned HTTP {(int)response.StatusCode}: {code ?? "unknown error"}.",
                        response.StatusCode, code, root.Clone(), outcomeUncertain: post && (int)response.StatusCode >= 500);
                }
                var value = root.Deserialize<T>(Json) ?? throw new JsonException("Empty response.");
                return new(response.StatusCode, value);
            }
        }
        catch (HttpRequestException ex)
        {
            throw new RobotException(post ? "Request outcome is unknown. Do not retry automatically." : "Robot is unreachable.",
                code: post ? "outcome_uncertain" : "unreachable", outcomeUncertain: post, inner: ex);
        }
        catch (OperationCanceledException ex) when (!token.IsCancellationRequested)
        {
            throw new RobotException(post ? "Request timed out; outcome is unknown. Do not retry automatically." : "Robot request timed out.",
                code: post ? "outcome_uncertain" : "timeout", outcomeUncertain: post, inner: ex);
        }
        catch (JsonException ex)
        {
            throw new RobotException(post ? "Invalid response; outcome is unknown. Do not retry automatically." : "Invalid robot response.",
                code: "invalid_response", outcomeUncertain: post, inner: ex);
        }
    }
}
