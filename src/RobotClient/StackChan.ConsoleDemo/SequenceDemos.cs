using StackChan.RobotClient;

internal static class SequenceDemos
{
    internal static async Task IntroductionAsync(StackChanClient robot, Action<string> progress,
        Func<CancellationToken, Task<bool>> stopRequested, CancellationToken cancellationToken)
    {
        await CheckStopAsync(stopRequested, cancellationToken);
        progress("Showing Hello from .NET");
        var text = await robot.ShowTextAsync("Hello from .NET", cancellationToken);
        await WaitAsync(async token => (await robot.GetTextStateAsync(text.Value, token)).State,
            stopRequested, cancellationToken);
        await CheckStopAsync(stopRequested, cancellationToken);
        progress("Setting purple RGB for five seconds");
        var rgb = await robot.SetRgbAsync("#800080", 5000, cancellationToken);
        await WaitAsync(async token => (await robot.GetRgbStateAsync(rgb.Value, token)).State,
            stopRequested, cancellationToken);
        progress("Introduction completed; temporary effects have ended.");
    }

    internal static async Task ExpressionsAsync(StackChanClient robot, Action<string> progress,
        Func<CancellationToken, Task<bool>> stopRequested, CancellationToken cancellationToken)
    {
        foreach (var action in new[] { RobotAction.Smile, RobotAction.Sound, RobotAction.Wink, RobotAction.Neutral })
        {
            await CheckStopAsync(stopRequested, cancellationToken);
            progress($"Action: {action}");
            var result = await robot.PerformAsync(action, cancellationToken: cancellationToken);
            var receipt = result.Value.Command!;
            var boot = result.Value.BootId;
            await WaitAsync(async token =>
            {
                var status = await robot.GetStatusAsync(token);
                if (status.BootId != boot || status.Command?.CommandId != receipt.CommandId ||
                    status.Command.Action != receipt.Action)
                    throw new RobotException("Action identity changed; outcome is unknown.", code: "action_uncertain",
                        outcomeUncertain: true);
                return status.Command.State;
            }, stopRequested, cancellationToken);
        }
        progress("Expression sequence completed.");
    }

    private static async Task CheckStopAsync(Func<CancellationToken, Task<bool>> stopRequested, CancellationToken token)
    {
        token.ThrowIfCancellationRequested();
        if (await stopRequested(token))
            throw new RobotException("STOP requested; sequence ended. Physical stopping is not verified.", code: "demo_stopped");
    }

    private static async Task WaitAsync(Func<CancellationToken, Task<string>> readState,
        Func<CancellationToken, Task<bool>> stopRequested, CancellationToken cancellationToken)
    {
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        deadline.CancelAfter(TimeSpan.FromSeconds(15));
        try
        {
            while (true)
            {
                await CheckStopAsync(stopRequested, deadline.Token);
                string state = await readState(deadline.Token);
                if (state == "completed") return;
                if (state is "failed" or "canceled" or "cancelled")
                    throw new RobotException($"Sequence stopped: operation {state}.", code: "demo_operation_stopped");
                if (state is not ("queued" or "displaying" or "active" or "accepted"))
                    throw new RobotException("Operation state is unknown; do not retry.", code: "demo_uncertain",
                        outcomeUncertain: true);
                await Task.Delay(250, deadline.Token);
            }
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
        {
            throw new RobotException("Operation observation timed out; do not retry.", code: "demo_uncertain",
                outcomeUncertain: true);
        }
    }
}
