using System.Net.Http;
using System.Runtime.InteropServices;
using System.Windows.Forms;

internal static class Program
{
    // Replace with the IP printed by ESP-01 Serial Monitor.
    private const string EspUrl = "http://192.168.29.123";
    private static readonly HttpClient Client = new() { Timeout = TimeSpan.FromSeconds(2) };

    // Synchronous Main remains on the STA thread required by Clipboard.
    [STAThread]
    private static void Main()
    {
        Console.Title = "ESP-01 Windows Receiver";
        Console.WriteLine($"ESP-01 receiver started: {EspUrl}");
        Console.WriteLine("Press Ctrl+C to exit.");

        try
        {
            string mode = Client.GetStringAsync($"{EspUrl}/status").GetAwaiter().GetResult().Trim();
            Console.WriteLine($"[ESP-01] Mode: {mode.ToUpperInvariant()}");
            if (mode == "copy")
            {
                string text = Client.GetStringAsync($"{EspUrl}/text").GetAwaiter().GetResult().Trim();
                Console.WriteLine($"[ESP-01] Configured text: {(string.IsNullOrEmpty(text) ? "(EMPTY)" : $"\"{text}\"")}");
            }
        }
        catch
        {
            // Device may still be connecting to Wi-Fi.
        }

        while (true)
        {
            try
            {
                string raw = Client.GetStringAsync($"{EspUrl}/event")
                    .GetAwaiter().GetResult();
                string eventName = raw.TrimEnd('\r', '\n');

                if (eventName.Length > 0)
                    HandleEvent(eventName);
            }
            catch (Exception ex) when (
                ex is HttpRequestException ||
                ex is TaskCanceledException)
            {
                // Device can be temporarily offline or rebooting.
            }

            Thread.Sleep(80);
        }
    }

    private static void HandleEvent(string eventName)
    {
        Console.WriteLine($"--> ESP-01 Event: {eventName}");
        try
        {
            switch (eventName)
            {
                case "COPY":
                    PressCopy();
                    break;

                case "WIN_V":
                    PressWindowsV();
                    break;

                case "SPACE":
                    TypeUnicode(" ");
                    Console.WriteLine("[Keyboard] Typed: [SPACE]");
                    break;

                default:
                    if (eventName.StartsWith("TYPE:", StringComparison.Ordinal))
                    {
                        string value = eventName[5..];
                        if (value.Length > 0)
                        {
                            TypeUnicode(value);
                            Console.WriteLine(value == " " ? "[Keyboard] Typed: [SPACE]" : $"[Keyboard] Typed: '{value}'");
                        }
                    }
                    else
                    {
                        Console.WriteLine($"[Unknown Event] {eventName}");
                    }
                    break;
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Action failed: {ex.Message}");
        }
    }

    private const uint INPUT_KEYBOARD = 1;
    private const uint KEYEVENTF_EXTENDEDKEY = 0x0001;
    private const uint KEYEVENTF_KEYUP = 0x0002;
    private const uint KEYEVENTF_UNICODE = 0x0004;

    private static void PressCopy()
    {
        const ushort VK_CONTROL = 0x11;
        const ushort VK_C = 0x43;

        // Send Ctrl + C to the active window where text is highlighted
        SendKey(VK_CONTROL, true);
        Thread.Sleep(20);
        SendKey(VK_C, true);
        Thread.Sleep(20);
        SendKey(VK_C, false);
        Thread.Sleep(20);
        SendKey(VK_CONTROL, false);

        // Give the active application a moment to write the selection to clipboard
        Thread.Sleep(60);
        try
        {
            string copied = Clipboard.GetText();
            if (!string.IsNullOrEmpty(copied))
            {
                string preview = copied.Length > 60 ? copied[..60] + "..." : copied;
                Console.WriteLine($"[Clipboard] Copied selection: \"{preview.Replace("\r", " ").Replace("\n", " ")}\"");
                // Sync to ESP-01 web interface
                _ = Client.GetAsync($"{EspUrl}/save?text={Uri.EscapeDataString(copied)}");
            }
            else
            {
                Console.WriteLine("[Clipboard] Sent Ctrl+C (no text was highlighted or selection was empty).");
            }
        }
        catch
        {
            Console.WriteLine("[Clipboard] Sent Ctrl+C.");
        }
    }

    private static void PressWindowsV()
    {
        const ushort VK_LWIN = 0x5B;
        const ushort VK_V = 0x56;

        // Windows Explorer hotkey dispatcher requires the modifier key to be
        // registered as held down before V arrives.
        SendKey(VK_LWIN, true);
        Thread.Sleep(25);
        SendKey(VK_V, true);
        Thread.Sleep(25);
        SendKey(VK_V, false);
        Thread.Sleep(25);
        SendKey(VK_LWIN, false);

        Console.WriteLine("[Keyboard] Dispatched Win+V (Clipboard History).");
    }

    private static void SendKey(ushort key, bool down)
    {
        uint flags = down ? 0u : KEYEVENTF_KEYUP;
        if (key is 0x5B or 0x5C)
            flags |= KEYEVENTF_EXTENDEDKEY;

        INPUT input = new()
        {
            type = INPUT_KEYBOARD,
            U = new INPUTUNION
            {
                ki = new KEYBDINPUT
                {
                    wVk = key,
                    dwFlags = flags
                }
            }
        };

        if (SendInput(1, [input], Marshal.SizeOf<INPUT>()) != 1)
        {
            int err = Marshal.GetLastWin32Error();
            Console.WriteLine($"Windows rejected keyboard input (win32 error: {err}).");
        }
    }

    private static void TypeUnicode(string value)
    {
        var inputs = new List<INPUT>(value.Length * 2);
        foreach (char character in value)
        {
            inputs.Add(new INPUT
            {
                type = INPUT_KEYBOARD,
                U = new INPUTUNION
                {
                    ki = new KEYBDINPUT
                    {
                        wVk = 0,
                        wScan = character,
                        dwFlags = KEYEVENTF_UNICODE
                    }
                }
            });
            inputs.Add(new INPUT
            {
                type = INPUT_KEYBOARD,
                U = new INPUTUNION
                {
                    ki = new KEYBDINPUT
                    {
                        wVk = 0,
                        wScan = character,
                        dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP
                    }
                }
            });
        }

        if (inputs.Count > 0)
        {
            uint sent = SendInput((uint)inputs.Count, [.. inputs], Marshal.SizeOf<INPUT>());
            if (sent != inputs.Count)
            {
                int err = Marshal.GetLastWin32Error();
                Console.WriteLine($"Windows rejected Unicode input (sent {sent}/{inputs.Count}, win32 error: {err}).");
            }
        }
    }

    [DllImport("user32.dll", SetLastError = true)]
    private static extern uint SendInput(uint nInputs, INPUT[] pInputs, int cbSize);

    [StructLayout(LayoutKind.Sequential)]
    private struct INPUT
    {
        public uint type;
        public INPUTUNION U;
    }

    [StructLayout(LayoutKind.Explicit)]
    private struct INPUTUNION
    {
        [FieldOffset(0)]
        public MOUSEINPUT mi;

        [FieldOffset(0)]
        public KEYBDINPUT ki;

        [FieldOffset(0)]
        public HARDWAREINPUT hi;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct MOUSEINPUT
    {
        public int dx;
        public int dy;
        public uint mouseData;
        public uint dwFlags;
        public uint time;
        public nint dwExtraInfo;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct KEYBDINPUT
    {
        public ushort wVk;
        public ushort wScan;
        public uint dwFlags;
        public uint time;
        public nint dwExtraInfo;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct HARDWAREINPUT
    {
        public uint uMsg;
        public ushort wParamL;
        public ushort wParamH;
    }
}
