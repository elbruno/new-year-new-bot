using System.Net;
using System.Net.Http.Json;
using System.Net.Sockets;
using System.Text.Json.Serialization;

namespace Episode1;

public enum RobotAction { Smile, Wink, Neutral }

public sealed record RobotVersion(string FirmwareVersion, int ProtocolVersion);
public sealed record CommandReceipt(uint CommandId, string Action, int DurationMs, string State,
    string? Error, uint? ConfigRevision);
public sealed record RobotStatus(
    [property: JsonRequired] int Version,
    [property: JsonRequired] uint BootId,
    [property: JsonRequired] string[] Capabilities,
    CommandReceipt? Command,
    string? FirmwareVersion);
public sealed record RobotResult<T>(HttpStatusCode StatusCode, T Value);

public sealed class RobotClient
{
    private readonly HttpClient http;
    private readonly Uri origin;
    private readonly SemaphoreSlim commandGate = new(1, 1);

    public RobotClient(HttpClient http, string address)
    {
        ArgumentNullException.ThrowIfNull(http);
        if (!IPAddress.TryParse(address, out var ip) || ip.AddressFamily != AddressFamily.InterNetwork ||
            ip.ToString() != address || !IsPrivate(ip))
            throw new ArgumentException("Supply a private IPv4 address.", nameof(address));
        this.http = http;
        origin = new Uri($"http://{address}/");
    }

    private static bool IsPrivate(IPAddress address)
    {
        byte[] bytes = address.GetAddressBytes();
        return bytes[0] == 10 || bytes[0] == 172 && bytes[1] is >= 16 and <= 31 ||
            bytes[0] == 192 && bytes[1] == 168;
    }

    public async Task<RobotVersion> GetVersionAsync(CancellationToken cancellationToken = default)
    {
        var version = await http.GetFromJsonAsync<RobotVersion>(new Uri(origin, "api/version"), cancellationToken);
        if (version is null || string.IsNullOrWhiteSpace(version.FirmwareVersion) || version.ProtocolVersion <= 0)
            throw new InvalidDataException("Invalid robot version response.");
        return version;
    }

    public async Task<RobotStatus> GetStatusAsync(CancellationToken cancellationToken = default)
    {
        var status = await http.GetFromJsonAsync<RobotStatus>(new Uri(origin, "api/status"), cancellationToken);
        if (status is null || status.BootId == 0 || status.Capabilities is null)
            throw new InvalidDataException("Invalid robot status response.");
        return status;
    }

    public async Task<RobotResult<RobotStatus>> PerformAsync(RobotAction action, int durationMs = 1000,
        CancellationToken cancellationToken = default)
    {
        if (durationMs is < 100 or > 5000) throw new ArgumentOutOfRangeException(nameof(durationMs));
        string name = action switch
        {
            RobotAction.Smile => "smile",
            RobotAction.Wink => "wink",
            RobotAction.Neutral => "neutral",
            _ => throw new ArgumentOutOfRangeException(nameof(action))
        };

        await commandGate.WaitAsync(cancellationToken);
        try
        {
            RobotStatus status = await GetStatusAsync(cancellationToken);
            if (!status.Capabilities.Contains(name, StringComparer.Ordinal))
                throw new InvalidOperationException($"Action is not advertised by this firmware: {name}.");

            uint lastId = status.Command?.CommandId ?? 0;
            if (lastId == uint.MaxValue) throw new InvalidOperationException("Command IDs are exhausted.");
            uint commandId = lastId + 1;
            HttpResponseMessage response;
            try
            {
                response = await http.PostAsJsonAsync(new Uri(origin, "api/actions"),
                    new { bootId = status.BootId, commandId, action = name, durationMs },
                    cancellationToken);
            }
            catch (Exception ex) when (ex is HttpRequestException or TaskCanceledException)
            {
                throw new CommandOutcomeUncertainException(
                    "The action POST outcome is uncertain. Inspect the robot before any manual retry.", ex);
            }

            using (response)
            {
                if (response.StatusCode is not (HttpStatusCode.OK or HttpStatusCode.Accepted))
                {
                    if ((int)response.StatusCode >= 500)
                        throw new CommandOutcomeUncertainException(
                            "The server returned an error after the action POST. Inspect the robot before any manual retry.");
                    throw new HttpRequestException($"Action request rejected: HTTP {(int)response.StatusCode}.");
                }

                RobotStatus result;
                try
                {
                    result = await response.Content.ReadFromJsonAsync<RobotStatus>(cancellationToken)
                        ?? throw new InvalidDataException("Empty action response.");
                }
                catch (Exception ex) when (ex is JsonException or InvalidDataException or TaskCanceledException)
                {
                    throw new CommandOutcomeUncertainException(
                        "The action response could not be verified. Inspect the robot before any manual retry.", ex);
                }

                CommandReceipt? receipt = result.Command;
                if (result.BootId != status.BootId || receipt is null || receipt.CommandId != commandId ||
                    receipt.Action != name || receipt.DurationMs != durationMs)
                    throw new CommandOutcomeUncertainException(
                        "The action response could not be correlated. Inspect the robot before any manual retry.");
                return new RobotResult<RobotStatus>(response.StatusCode, result);
            }
        }
        finally
        {
            commandGate.Release();
        }
    }
}

public sealed class CommandOutcomeUncertainException(string message, Exception? inner = null)
    : Exception(message, inner);
