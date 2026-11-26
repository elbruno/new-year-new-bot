// Goal: Encode the microphone demo's PCM16 payload as a standard WAV file.
// Responsibilities: Validate the bounded payload and write a 44-byte PCM header.
// How it works: Header fields are little-endian and audio bytes follow unchanged.
using System.Buffers.Binary;

internal static class MicrophoneWav
{
    internal const int SampleRate = 16000;
    internal const int Channels = 1;
    internal const int BitsPerSample = 16;
    internal const int ExpectedPcmBytes = 96000;
    internal const int HeaderBytes = 44;

    internal static byte[] Create(ReadOnlySpan<byte> pcm)
    {
        if (pcm.Length != ExpectedPcmBytes || (pcm.Length & 1) != 0)
            throw new ArgumentException("Expected 96,000 bytes of mono PCM16 at 16 kHz.", nameof(pcm));

        byte[] wav = new byte[HeaderBytes + pcm.Length];
        "RIFF"u8.CopyTo(wav);
        BinaryPrimitives.WriteUInt32LittleEndian(wav.AsSpan(4), (uint)(wav.Length - 8));
        "WAVEfmt "u8.CopyTo(wav.AsSpan(8));
        BinaryPrimitives.WriteUInt32LittleEndian(wav.AsSpan(16), 16);
        BinaryPrimitives.WriteUInt16LittleEndian(wav.AsSpan(20), 1);
        BinaryPrimitives.WriteUInt16LittleEndian(wav.AsSpan(22), Channels);
        BinaryPrimitives.WriteUInt32LittleEndian(wav.AsSpan(24), SampleRate);
        BinaryPrimitives.WriteUInt32LittleEndian(wav.AsSpan(28), SampleRate * Channels * BitsPerSample / 8);
        BinaryPrimitives.WriteUInt16LittleEndian(wav.AsSpan(32), Channels * BitsPerSample / 8);
        BinaryPrimitives.WriteUInt16LittleEndian(wav.AsSpan(34), BitsPerSample);
        "data"u8.CopyTo(wav.AsSpan(36));
        BinaryPrimitives.WriteUInt32LittleEndian(wav.AsSpan(40), (uint)pcm.Length);
        pcm.CopyTo(wav.AsSpan(HeaderBytes));
        return wav;
    }
}
