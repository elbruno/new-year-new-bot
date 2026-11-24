// Goal: Define typed contracts shared by the robot client and its callers.
// Responsibilities: Represent API actions, status, receipts, and other data.
// How it works:
// 1. JSON responses are deserialized into the records defined here.
// 2. Required fields and typed values describe the expected API shape.
// 3. The client validates and returns these models to applications.
using System.Net;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace StackChan.RobotClient;

public enum RobotAction
{
    Neutral, Wink, Smile, LookUp, Sleep, Wake, Warning, Important, Sound,
    ImportantNotification, Nod, MoveLeft, MoveRight, MoveUp, MoveDown, Home
}

public enum BspMotionAction
{
    Left, Right, Up, Down, Home, MoveX, MoveY, MoveXY, NodYes, ShakeNo, Dance, GrumpyShake
}

public sealed record BspMotionRequest([property: JsonRequired] bool Accepted,
    [property: JsonRequired] uint BootId, [property: JsonRequired] uint RequestId,
    [property: JsonRequired] string Action, [property: JsonRequired] string State,
    [property: JsonRequired] string PhysicalResult);
public sealed record BspMotionState([property: JsonRequired] uint BootId, [property: JsonRequired] uint RequestId,
    [property: JsonRequired] bool AdmissionReady, [property: JsonRequired] string Action,
    [property: JsonRequired] string State, [property: JsonRequired] string? Error,
    [property: JsonRequired] bool GestureSubmitted, [property: JsonRequired] string PhysicalResult)
{
    public bool? DirectionReferenceReady { get; init; }
}

public sealed record RobotVersion(string FirmwareVersion, int ProtocolVersion);
public sealed record RobotNetwork(string State, bool Setup, string? Error);
public sealed record RobotPose(int Yaw, int Pitch);
public sealed record RobotMotion(string State, RobotPose? Position, string? Error, string? Mode,
    uint? ConfigRevision, string? PositionUnits);
public sealed record CommandReceipt(uint CommandId, string Action, int DurationMs, string State,
    string? Error, uint? ConfigRevision);
public sealed record StopReceipt(uint BootId, uint StopId, string Outcome);
public sealed record StopDiagnostic(StopReceipt StopReceipt, JsonElement Rail, string Torque, string PhysicalStop);
public sealed record StopResult(StopReceipt Receipt, RobotStatus? Status, StopDiagnostic? Diagnostic);
public sealed record RobotRoutine(string Name, bool Available, bool RequiresMotion);
public sealed record RobotStatus([property: JsonRequired] int Version, [property: JsonRequired] uint BootId,
    [property: JsonRequired] string[] Capabilities, CommandReceipt? Command,
    string? FirmwareVersion, RobotMotion? Motion, bool Quiet, StopReceipt? StopReceipt, RobotRoutine[]? Routines);
public sealed record DisplayBrightness(int Brightness, int Minimum, int Maximum, int Default, bool Pending,
    int? RequestedBrightness, bool Persistent, string? Error);
public sealed record ScreenSummary(string Id, string Title);
public sealed record ScreenList(string Current, ScreenSummary[] Screens);
public sealed record ScreenElement(string Id, string Label, string Type, bool Enabled, bool Motion);
public sealed record Screen(string Id, string Title, ScreenElement[] Elements);
public sealed record UiAcceptance(bool Accepted);
public sealed record AudioRequest([property: JsonRequired] bool Accepted, [property: JsonRequired] string State,
    [property: JsonRequired] uint BootId, [property: JsonRequired] uint RequestId);
public sealed record AudioFiles([property: JsonRequired] string[] Files, [property: JsonRequired] bool Truncated);
public sealed record AudioState([property: JsonRequired] uint BootId, [property: JsonRequired] uint RequestId,
    [property: JsonRequired] string State, [property: JsonRequired] string? Error);
public sealed record ImageFileList([property: JsonRequired] string[] Files, [property: JsonRequired] bool Truncated);
public sealed record ImageRequest([property: JsonRequired] bool Accepted, [property: JsonRequired] string State,
    [property: JsonRequired] uint BootId, [property: JsonRequired] uint RequestId);
public sealed record ImageState([property: JsonRequired] uint BootId, [property: JsonRequired] uint RequestId,
    [property: JsonRequired] string State, [property: JsonRequired] string? Error);
public sealed record TextRequest([property: JsonRequired] bool Accepted, [property: JsonRequired] string State,
    [property: JsonRequired] uint BootId, [property: JsonRequired] uint RequestId);
public sealed record TextState([property: JsonRequired] uint BootId, [property: JsonRequired] uint RequestId,
    [property: JsonRequired] string State, [property: JsonRequired] string? Error);
public sealed record RgbRequest([property: JsonRequired] bool Accepted, [property: JsonRequired] string State,
    [property: JsonRequired] uint BootId, [property: JsonRequired] uint RequestId);
public sealed record RgbState([property: JsonRequired] uint BootId, [property: JsonRequired] uint RequestId,
    [property: JsonRequired] string State, [property: JsonRequired] string? Error);
public sealed record TopSensor([property: JsonRequired] bool Initialized, [property: JsonRequired] bool Available,
    [property: JsonRequired] string? Error, [property: JsonRequired] bool HasSample,
    [property: JsonRequired] bool Fresh, [property: JsonRequired] uint? AgeMs,
    [property: JsonRequired] int? Front, [property: JsonRequired] int? Middle, [property: JsonRequired] int? Back);
public sealed record LightSensor([property: JsonRequired] bool Initialized, [property: JsonRequired] bool Available,
    [property: JsonRequired] string? Error, [property: JsonRequired] bool HasSample,
    [property: JsonRequired] bool Fresh, [property: JsonRequired] uint? AgeMs,
    [property: JsonRequired] int? Proximity, [property: JsonRequired] int? Als0, [property: JsonRequired] int? Als1);
public sealed record SensorRgb([property: JsonRequired] bool Active, [property: JsonRequired] uint? DurationMs,
    [property: JsonRequired] string? Error);
public sealed record PettingDiagnostics([property: JsonRequired] bool Enabled, [property: JsonRequired] bool SoundEnabled,
    [property: JsonRequired] bool Quiet, [property: JsonRequired] bool Eligible,
    [property: JsonRequired] string? IneligibleReason, [property: JsonRequired] bool Settled,
    [property: JsonRequired] bool Released, [property: JsonRequired] int TouchCount,
    [property: JsonRequired] int ReleaseCount, [property: JsonRequired] bool Active,
    [property: JsonRequired] int Stage, [property: JsonRequired] string SoundState);
public sealed record ImuVector([property: JsonRequired] bool HasSample, [property: JsonRequired] bool Fresh,
    [property: JsonRequired] uint? AgeMs, [property: JsonRequired] double? X,
    [property: JsonRequired] double? Y, [property: JsonRequired] double? Z);
public sealed record ImuDiagnostics([property: JsonRequired] bool Available, [property: JsonRequired] string? Error,
    [property: JsonRequired] ImuVector AccelerationG, [property: JsonRequired] ImuVector GyroscopeDps);
public sealed record SensorDiagnostics([property: JsonRequired] TopSensor Top, [property: JsonRequired] LightSensor Light,
    [property: JsonRequired] SensorRgb Rgb, [property: JsonRequired] PettingDiagnostics Petting)
{
    public ImuDiagnostics? Imu { get; init; }
}
public sealed record RobotResult<T>(HttpStatusCode StatusCode, T Value);

public sealed class RobotException : Exception
{
    public RobotException(string message, HttpStatusCode? statusCode = null, string? code = null,
        JsonElement? response = null, bool outcomeUncertain = false, Exception? inner = null)
        : base(message, inner)
    {
        StatusCode = statusCode;
        Code = code;
        Response = response;
        OutcomeUncertain = outcomeUncertain;
    }

    public HttpStatusCode? StatusCode { get; }
    public string? Code { get; }
    public JsonElement? Response { get; }
    public bool OutcomeUncertain { get; }
}
