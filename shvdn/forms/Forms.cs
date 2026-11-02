// Stand-in for the parts of System.Windows.Forms used by ScriptHookV .NET and script mods on the iOS port.
// Values match Windows Forms exactly, since mods compare and store them.
using System;
using System.ComponentModel;

namespace System.Windows.Forms
{
    [Flags]
    public enum Keys
    {
        KeyCode = 0x0000FFFF, Modifiers = unchecked((int)0xFFFF0000), None = 0,
        LButton = 1, RButton = 2, Cancel = 3, MButton = 4, XButton1 = 5, XButton2 = 6,
        Back = 8, Tab = 9, LineFeed = 10, Clear = 12, Return = 13, Enter = 13,
        ShiftKey = 16, ControlKey = 17, Menu = 18, Pause = 19, Capital = 20, CapsLock = 20,
        KanaMode = 21, HanguelMode = 21, HangulMode = 21, JunjaMode = 23, FinalMode = 24, HanjaMode = 25, KanjiMode = 25,
        Escape = 27, IMEConvert = 28, IMENonconvert = 29, IMEAccept = 30, IMEAceept = 30, IMEModeChange = 31,
        Space = 32, Prior = 33, PageUp = 33, Next = 34, PageDown = 34, End = 35, Home = 36,
        Left = 37, Up = 38, Right = 39, Down = 40, Select = 41, Print = 42, Execute = 43, Snapshot = 44, PrintScreen = 44,
        Insert = 45, Delete = 46, Help = 47,
        D0 = 48, D1, D2, D3, D4, D5, D6, D7, D8, D9,
        A = 65, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
        LWin = 91, RWin = 92, Apps = 93, Sleep = 95,
        NumPad0 = 96, NumPad1, NumPad2, NumPad3, NumPad4, NumPad5, NumPad6, NumPad7, NumPad8, NumPad9,
        Multiply = 106, Add = 107, Separator = 108, Subtract = 109, Decimal = 110, Divide = 111,
        F1 = 112, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24,
        NumLock = 144, Scroll = 145,
        LShiftKey = 160, RShiftKey = 161, LControlKey = 162, RControlKey = 163, LMenu = 164, RMenu = 165,
        BrowserBack = 166, BrowserForward = 167, BrowserRefresh = 168, BrowserStop = 169, BrowserSearch = 170,
        BrowserFavorites = 171, BrowserHome = 172, VolumeMute = 173, VolumeDown = 174, VolumeUp = 175,
        MediaNextTrack = 176, MediaPreviousTrack = 177, MediaStop = 178, MediaPlayPause = 179,
        LaunchMail = 180, SelectMedia = 181, LaunchApplication1 = 182, LaunchApplication2 = 183,
        OemSemicolon = 186, Oem1 = 186, Oemplus = 187, Oemcomma = 188, OemMinus = 189, OemPeriod = 190,
        OemQuestion = 191, Oem2 = 191, Oemtilde = 192, Oem3 = 192, OemOpenBrackets = 219, Oem4 = 219,
        OemPipe = 220, Oem5 = 220, OemCloseBrackets = 221, Oem6 = 221, OemQuotes = 222, Oem7 = 222, Oem8 = 223,
        OemBackslash = 226, Oem102 = 226, ProcessKey = 229, Packet = 231,
        Attn = 246, Crsel = 247, Exsel = 248, EraseEof = 249, Play = 250, Zoom = 251, NoName = 252, Pa1 = 253, OemClear = 254,
        Shift = 0x00010000, Control = 0x00020000, Alt = 0x00040000,
    }

    public class KeyEventArgs : EventArgs
    {
        public KeyEventArgs(Keys keyData) { KeyData = keyData; }
        public Keys KeyData { get; }
        public Keys KeyCode => KeyData & Keys.KeyCode;
        public Keys Modifiers => KeyData & Keys.Modifiers;
        public int KeyValue => (int)(KeyData & Keys.KeyCode);
        public bool Shift => (KeyData & Keys.Shift) != 0;
        public bool Control => (KeyData & Keys.Control) != 0;
        public bool Alt => (KeyData & Keys.Alt) != 0;
        public bool Handled { get; set; }
        public bool SuppressKeyPress { get => _suppress; set { _suppress = value; Handled = value; } }
        private bool _suppress;
    }

    public delegate void KeyEventHandler(object sender, KeyEventArgs e);

    public class KeyPressEventArgs : EventArgs
    {
        public KeyPressEventArgs(char keyChar) { KeyChar = keyChar; }
        public char KeyChar { get; set; }
        public bool Handled { get; set; }
    }
    public delegate void KeyPressEventHandler(object sender, KeyPressEventArgs e);

    // Some mods show a message box on errors: there's no desktop, so it goes to the ScriptHookV .NET log.
    public enum DialogResult { None, OK, Cancel, Abort, Retry, Ignore, Yes, No, TryAgain = 10, Continue = 11 }
    public enum MessageBoxButtons { OK, OKCancel, AbortRetryIgnore, YesNoCancel, YesNo, RetryCancel, CancelTryContinue }
    public enum MessageBoxIcon { None = 0, Hand = 16, Stop = 16, Error = 16, Question = 32, Exclamation = 48, Warning = 48, Asterisk = 64, Information = 64 }
    public static class MessageBox
    {
        public static Action<string> Sink;
        public static DialogResult Show(string text) { Sink?.Invoke(text); return DialogResult.OK; }
        public static DialogResult Show(string text, string caption) { Sink?.Invoke(caption + ": " + text); return DialogResult.OK; }
        public static DialogResult Show(string text, string caption, MessageBoxButtons buttons) => Show(text, caption);
        public static DialogResult Show(string text, string caption, MessageBoxButtons buttons, MessageBoxIcon icon) => Show(text, caption);
    }

    public static class Clipboard
    {
        private static string s_text = "";
        public static string GetText() => s_text;
        public static void SetText(string text) { s_text = text ?? ""; }
        public static bool ContainsText() => s_text.Length > 0;
    }

    public class Control
    {
        public static Keys ModifierKeys => Keys.None;
        public static System.Drawing.Point MousePosition => Cursor.Position;
        public static bool IsKeyLocked(Keys keyVal) => false;
    }

    // the phone's screen, as mods ask for it (NativeUI centers the mouse cursor with it)
    public class Screen
    {
        public static Screen PrimaryScreen { get; } = new Screen();
        public static Screen[] AllScreens => new[] { PrimaryScreen };
        public static Screen FromPoint(System.Drawing.Point p) => PrimaryScreen;
        public System.Drawing.Rectangle Bounds => new System.Drawing.Rectangle(0, 0, 1920, 1080);
        public System.Drawing.Rectangle WorkingArea => Bounds;
        public bool Primary => true;
        public string DeviceName => "iPhone";
        public int BitsPerPixel => 32;
    }

    // there's no mouse pointer on the phone: the position is just remembered
    public sealed class Cursor
    {
        public static System.Drawing.Point Position { get; set; }
        public static System.Drawing.Rectangle Clip { get; set; }
        public static void Show() { }
        public static void Hide() { }
    }
}
