// Goal: Download one bounded microphone recording from StackChan.
// Responsibilities: Check firmware and capture state, validate PCM, save WAV, and open it.
// How it works: HTTP requests are correlated with the firmware's bootId and requestId.
using System.Buffers.Binary;
using System.Diagnostics;
using System.Net;
using System.Net.Http.Json;
using System.Text.Json;

internal static class Program
{
    private const int CaptureDurationMs = 3000;
    private const int ExpectedPcmBytes = 96000;
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);

    private sealed record CaptureReceipt(
        int Version,
        uint BootId,
        uint RequestId,
        int DurationMs,
        int SampleRate,
        int Channels,
        int BitsPerSample,
        string State,
        int ByteLength,
        string? Error,
        bool? DownloadAvailable);

    private static async Task<int> Main(string[] args)
    {
        if (args is ["--self-test"])
        {
            await RunSelfTestAsync();
            return 0;
        }

        if (args.Length != 1 || !TryCreateEndpoint(args[0], out Uri? endpoint) || endpoint is null)
        {
            Console.Error.WriteLine("Usage: dotnet run -- <http://robot-address> | --self-test");
            return 2;
        }

        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(15) };
        using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(20));
        try
        {
            await CaptureAsync(client, endpoint, deadline.Token);
            return 0;
        }
        catch (Exception exception) when (exception is HttpRequestException or JsonException or
                                           IOException or InvalidOperationException or ArgumentException or
                                           TimeoutException or OperationCanceledException or
                                           System.ComponentModel.Win32Exception)
        {
            Console.Error.WriteLine(exception.Message);
            return 1;
        }
    }

    private static bool TryCreateEndpoint(string address, out Uri? endpoint)
    {
        endpoint = null;
        if (!Uri.TryCreate(address, UriKind.Absolute, out var parsed) ||
            parsed.Scheme != Uri.UriSchemeHttp || parsed.UserInfo.Length != 0 ||
            parsed.AbsolutePath != "/" || parsed.Query.Length != 0 || parsed.Fragment.Length != 0)
            return false;

        endpoint = new Uri(parsed.GetLeftPart(UriPartial.Authority) + "/", UriKind.Absolute);
        return true;
    }

    private static async Task CaptureAsync(HttpClient client, Uri endpoint, CancellationToken cancellationToken)
    {
        using JsonDocument version = await GetJsonAsync(client, endpoint, "api/version", cancellationToken);
        var root = version.RootElement;
        string firmwareVersion = root.GetProperty("firmwareVersion").GetString()
            ?? throw new InvalidDataException("Firmware version is missing.");
        if (!Version.TryParse(firmwareVersion, out var parsedVersion) ||
            parsedVersion < new Version(0, 6, 1))
            throw new InvalidOperationException($"Microphone capture requires firmware 0.6.1 or later; found {firmwareVersion}.");

        uint bootId = root.GetProperty("bootId").GetUInt32();
        if (bootId == 0) throw new InvalidDataException("Firmware returned an invalid bootId.");

        CaptureReceipt current = await ReadCaptureAsync(client, endpoint, null, null, cancellationToken);
        ValidateReceipt(current, bootId, current.RequestId);
        if (current.State is "queued" or "starting" or "recording" or "canceling" or "stopping")
            throw new InvalidOperationException("A microphone capture is already active; check its current status first.");
        if (current.RequestId == uint.MaxValue)
            throw new InvalidOperationException("The microphone requestId counter is exhausted for this boot.");
        uint requestId = current.RequestId + 1;
        Console.WriteLine(
            $"Firmware {firmwareVersion}, boot {bootId}. Microphone status route is available; checking capture readiness.");

        using var start = new HttpRequestMessage(HttpMethod.Post, new Uri(endpoint, "api/microphone/capture"))
        {
            Content = JsonContent.Create(new { bootId, requestId, durationMs = CaptureDurationMs })
        };
        using var startResponse = await client.SendAsync(start, cancellationToken);
        if (startResponse.StatusCode != HttpStatusCode.Accepted)
            throw new InvalidOperationException(await DescribeErrorAsync(startResponse, cancellationToken));

        CaptureReceipt accepted = await ReadReceiptAsync(startResponse, cancellationToken);
        ValidateReceipt(accepted, bootId, requestId);
        if (accepted.State != "queued" || accepted.DurationMs != CaptureDurationMs ||
            accepted.ByteLength != 0)
            throw new InvalidDataException("The microphone start receipt did not match the requested capture.");

        Console.WriteLine("Recording for three seconds...");
        CaptureReceipt completed = await WaitForCompletionAsync(
            client, endpoint, bootId, requestId, cancellationToken);
        if (completed.DownloadAvailable != true || completed.DurationMs != CaptureDurationMs ||
            completed.ByteLength != ExpectedPcmBytes)
            throw new InvalidDataException(
                $"Completed capture is not downloadable PCM ({completed.ByteLength} bytes).");

        byte[] pcm = await DownloadPcmAsync(client, endpoint, completed, cancellationToken);
        byte[] wav = MicrophoneWav.Create(pcm);
        string outputDirectory = Path.Combine(Environment.CurrentDirectory, "microphone-recordings");
        Directory.CreateDirectory(outputDirectory);
        string path = Path.GetFullPath(Path.Combine(outputDirectory,
            $"stackchan-microphone-{DateTime.UtcNow:yyyyMMdd-HHmmssfff}-{bootId}-{requestId}.wav"));
        await SaveWavAsync(path, wav, cancellationToken);

        Console.WriteLine($"Saved {wav.Length} bytes to {path}");
        if (Process.Start(new ProcessStartInfo { FileName = path, UseShellExecute = true }) is null)
            throw new InvalidOperationException($"WAV file was saved but the default audio player did not open it: {path}");
    }

    private static async Task<JsonDocument> GetJsonAsync(
        HttpClient client, Uri endpoint, string path, CancellationToken cancellationToken)
    {
        using var response = await client.GetAsync(new Uri(endpoint, path), cancellationToken);
        if (response.StatusCode != HttpStatusCode.OK)
            throw new InvalidOperationException(await DescribeErrorAsync(response, cancellationToken));
        await using Stream stream = await response.Content.ReadAsStreamAsync(cancellationToken);
        return await JsonDocument.ParseAsync(stream, cancellationToken: cancellationToken);
    }

    private static async Task<CaptureReceipt> ReadCaptureAsync(
        HttpClient client, Uri endpoint, uint? bootId, uint? requestId, CancellationToken cancellationToken)
    {
        string path = bootId.HasValue
            ? $"api/microphone/capture?bootId={bootId.Value}&requestId={requestId!.Value}"
            : "api/microphone/capture";
        using var response = await client.GetAsync(new Uri(endpoint, path), cancellationToken);
        if (response.StatusCode != HttpStatusCode.OK)
            throw new InvalidOperationException(await DescribeErrorAsync(response, cancellationToken));
        return await ReadReceiptAsync(response, cancellationToken);
    }

    private static async Task<CaptureReceipt> ReadReceiptAsync(
        HttpResponseMessage response, CancellationToken cancellationToken)
    {
        CaptureReceipt? receipt = await response.Content.ReadFromJsonAsync<CaptureReceipt>(
            JsonOptions, cancellationToken);
        return receipt ?? throw new InvalidDataException("Firmware returned an empty microphone receipt.");
    }

    private static void ValidateReceipt(CaptureReceipt receipt, uint bootId, uint requestId)
    {
        bool knownState = receipt.State is "idle" or "queued" or "starting" or "recording" or
            "canceling" or "stopping" or "completed" or "canceled" or "failed" or "uncertain";
        bool validLength = receipt.State == "completed"
            ? receipt.ByteLength is >= 2 and <= ExpectedPcmBytes && (receipt.ByteLength & 1) == 0
            : receipt.ByteLength == 0;
        if (receipt.Version != 1 || receipt.BootId != bootId || receipt.RequestId != requestId ||
            (receipt.RequestId == 0) != (receipt.State == "idle") ||
            receipt.SampleRate != 16000 || receipt.Channels != 1 || receipt.BitsPerSample != 16 ||
            receipt.DurationMs is < 0 or > CaptureDurationMs || !knownState || !validLength ||
            receipt.DownloadAvailable is null ||
            receipt.DownloadAvailable == true && receipt.State != "completed")
            throw new InvalidDataException("Microphone receipt format or boot/request identity is invalid.");
    }

    private static async Task<CaptureReceipt> WaitForCompletionAsync(
        HttpClient client, Uri endpoint, uint bootId, uint requestId, CancellationToken cancellationToken)
    {
        DateTime deadline = DateTime.UtcNow.AddSeconds(10);
        while (DateTime.UtcNow < deadline)
        {
            CaptureReceipt state = await ReadCaptureAsync(
                client, endpoint, bootId, requestId, cancellationToken);
            ValidateReceipt(state, bootId, requestId);
            if (state.State == "completed") return state;
            if (state.State is "failed" or "canceled" or "uncertain")
                throw new InvalidOperationException(
                    $"Microphone capture {state.State}: {state.Error ?? "no error detail"}.");
            await Task.Delay(200, cancellationToken);
        }

        throw new TimeoutException(
            $"Capture completion is unknown for boot {bootId}, request {requestId}; do not replay the POST.");
    }

    private static async Task<byte[]> DownloadPcmAsync(
        HttpClient client, Uri endpoint, CaptureReceipt capture, CancellationToken cancellationToken)
    {
        string path = $"api/microphone/capture/download?bootId={capture.BootId}&requestId={capture.RequestId}";
        using var response = await client.GetAsync(
            new Uri(endpoint, path), HttpCompletionOption.ResponseHeadersRead, cancellationToken);
        if (response.StatusCode != HttpStatusCode.OK)
            throw new InvalidOperationException(await DescribeErrorAsync(response, cancellationToken));

        if (response.Content.Headers.ContentType?.MediaType != "application/octet-stream" ||
            response.Content.Headers.ContentLength != capture.ByteLength ||
            capture.ByteLength is < 2 or > ExpectedPcmBytes || (capture.ByteLength & 1) != 0)
            throw new InvalidDataException("PCM response content type or length is invalid.");

        RequireHeader(response, "X-Microphone-Sample-Rate", "16000");
        RequireHeader(response, "X-Microphone-Channels", "1");
        RequireHeader(response, "X-Microphone-Bits-Per-Sample", "16");
        RequireHeader(response, "X-Microphone-Boot-Id", capture.BootId.ToString());
        RequireHeader(response, "X-Microphone-Request-Id", capture.RequestId.ToString());

        byte[] pcm = await response.Content.ReadAsByteArrayAsync(cancellationToken);
        if (pcm.Length != capture.ByteLength || pcm.Length != ExpectedPcmBytes)
            throw new InvalidDataException($"Expected 96,000 PCM bytes, received {pcm.Length}.");
        return pcm;
    }

    private static void RequireHeader(HttpResponseMessage response, string name, string expected)
    {
        string[] values = response.Headers.TryGetValues(name, out var headers) ? headers.ToArray() : [];
        if (values.Length != 1 || values[0] != expected)
            throw new InvalidDataException($"Microphone response header {name} is missing or invalid.");
    }

    private static async Task<string> DescribeErrorAsync(
        HttpResponseMessage response, CancellationToken cancellationToken)
    {
        string detail = await response.Content.ReadAsStringAsync(cancellationToken);
        try
        {
            using JsonDocument error = JsonDocument.Parse(detail);
            if (error.RootElement.TryGetProperty("error", out var value) &&
                value.ValueKind == JsonValueKind.String)
            {
                string code = value.GetString()!;
                if (code == "microphone_unavailable")
                    return "Microphone capture is unavailable on this board or firmware profile.";
                return $"HTTP {(int)response.StatusCode}: {code}";
            }
        }
        catch (JsonException)
        {
            // Return the status when the server does not send its standard JSON error.
        }
        return $"Microphone request returned HTTP {(int)response.StatusCode}.";
    }

    private static async Task SaveWavAsync(
        string path, ReadOnlyMemory<byte> wav, CancellationToken cancellationToken)
    {
        await using var file = new FileStream(
            path, FileMode.CreateNew, FileAccess.Write, FileShare.Read);
        await file.WriteAsync(wav, cancellationToken);
        await file.FlushAsync(cancellationToken);
    }

    private static async Task RunSelfTestAsync()
    {
        byte[] pcm = new byte[ExpectedPcmBytes];
        for (int i = 0; i < pcm.Length; i++) pcm[i] = (byte)(i & 0xFF);
        byte[] wav = MicrophoneWav.Create(pcm);

        if (wav.Length != 96044 || !wav.AsSpan(0, 4).SequenceEqual("RIFF"u8) ||
            BinaryPrimitives.ReadUInt32LittleEndian(wav.AsSpan(24)) != 16000 ||
            BinaryPrimitives.ReadUInt32LittleEndian(wav.AsSpan(40)) != ExpectedPcmBytes ||
            !wav.AsSpan(MicrophoneWav.HeaderBytes).SequenceEqual(pcm))
            throw new InvalidOperationException("WAV header or PCM payload self-test failed.");

        string tempPath = Path.Combine(Path.GetTempPath(), $"stackchan-mic-{Guid.NewGuid():N}.wav");
        try
        {
            await SaveWavAsync(tempPath, wav, CancellationToken.None);
            using var exclusiveRead = new FileStream(
                tempPath, FileMode.Open, FileAccess.Read, FileShare.None);
            if (exclusiveRead.Length != 96044)
                throw new InvalidOperationException("WAV file handle lifecycle self-test failed.");
        }
        finally
        {
            if (File.Exists(tempPath)) File.Delete(tempPath);
        }

        Console.WriteLine("PASS: 96,000 PCM bytes create a 96,044-byte WAV and the saved file is released.");
    }
}
