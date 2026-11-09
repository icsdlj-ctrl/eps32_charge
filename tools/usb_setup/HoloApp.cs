using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.IO.Ports;
using System.Net;
using System.Text;
using System.Threading;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Effects;
using System.Windows.Markup;
using System.Windows.Media.Imaging;
using System.Windows.Shapes;
using System.Runtime.InteropServices;
using System.Windows.Interop;
using IoPath = System.IO.Path;
using System.Windows.Threading;
using System.Web.Script.Serialization;

namespace HoloStation
{
    static class Palette
    {
        public static readonly SolidColorBrush Canvas = B(0xF5, 0xF5, 0xF7);
        public static readonly SolidColorBrush Side = B(0xEB, 0xEB, 0xF0);
        public static readonly SolidColorBrush Card = Brushes.White;
        public static readonly SolidColorBrush Ink = B(0x1D, 0x1D, 0x1F);
        public static readonly SolidColorBrush Muted = B(0x86, 0x86, 0x8B);
        public static readonly SolidColorBrush Hair = B(0xE5, 0xE5, 0xEA);
        public static readonly SolidColorBrush Well = B(0xF2, 0xF2, 0xF7);
        public static readonly SolidColorBrush Blue = B(0x00, 0x7A, 0xFF);
        public static readonly SolidColorBrush BlueHot = B(0x00, 0x6E, 0xE6);
        public static readonly SolidColorBrush Chip = B(0xE8, 0xE8, 0xED);
        public static readonly SolidColorBrush ChipHot = B(0xDE, 0xDE, 0xE3);
        public static readonly SolidColorBrush Pick = B(0xE8, 0xE8, 0xED);
        public static readonly SolidColorBrush Green = B(0x34, 0xC7, 0x59);
        public static readonly SolidColorBrush Red = B(0xFF, 0x3B, 0x30);
        public static readonly SolidColorBrush Orange = B(0xFF, 0x9F, 0x0A);
        public static readonly FontFamily Face = new FontFamily("Segoe UI, Microsoft YaHei UI");
        static SolidColorBrush B(byte r, byte g, byte b)
        {
            var brush = new SolidColorBrush(Color.FromRgb(r, g, b));
            brush.Freeze();
            return brush;
        }
    }

    static class Json
    {
        static readonly JavaScriptSerializer Ser = new JavaScriptSerializer();
        public static string Stringify(object value) { return Ser.Serialize(value); }
        public static Dictionary<string, object> Parse(string text)
        {
            return Ser.Deserialize<Dictionary<string, object>>(text);
        }
        public static Dictionary<string, object> Map(object value)
        {
            return value as Dictionary<string, object>;
        }
        public static string Str(Dictionary<string, object> map, string key)
        {
            object value;
            if (map == null || !map.TryGetValue(key, out value) || value == null) return "";
            return Convert.ToString(value);
        }
        public static bool Bool(object value)
        {
            if (value is bool) return (bool)value;
            return false;
        }
        public static int Int(object value)
        {
            if (value == null) return 0;
            return Convert.ToInt32(value);
        }
        public static double Num(object value)
        {
            if (value == null) return 0;
            return Convert.ToDouble(value, CultureInfo.InvariantCulture);
        }
        public static IEnumerable Items(object value)
        {
            var list = value as ArrayList;
            if (list == null) yield break;
            foreach (var item in list) yield return item;
        }
    }

    sealed class Row
    {
        public string Key;
        public string Title;
        public string Meta;
    }

    sealed class PickList : Border
    {
        readonly StackPanel stack = new StackPanel();
        public string SelectedKey;
        public event Action<string> Picked;
        public PickList()
        {
            Background = Palette.Card;
            CornerRadius = new CornerRadius(16);
            SnapsToDevicePixels = true;
            var scroll = new ScrollViewer
            {
                VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
                HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled,
                Padding = new Thickness(6, 6, 6, 6),
                Content = stack
            };
            Child = scroll;
        }
        public void SetItems(IList<Row> rows)
        {
            if (IsMouseOver) return;
            var keep = SelectedKey;
            stack.Children.Clear();
            SelectedKey = null;
            if (rows.Count == 0)
            {
                stack.Children.Add(new TextBlock { Text = "这里还是空的", FontSize = 13, Foreground = Palette.Muted, Margin = new Thickness(12, 10, 12, 0) });
                return;
            }
            foreach (var row in rows)
            {
                var title = new TextBlock { Text = row.Title, FontSize = 13, Foreground = Palette.Ink, VerticalAlignment = VerticalAlignment.Center };
                var meta = new TextBlock { Text = row.Meta ?? "", FontSize = 12, Foreground = Palette.Muted, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(12, 0, 0, 0) };
                var grid = new Grid();
                grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
                grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
                Grid.SetColumn(meta, 1);
                grid.Children.Add(title);
                grid.Children.Add(meta);
                var item = new Border
                {
                    CornerRadius = new CornerRadius(10),
                    Padding = new Thickness(12, 8, 12, 8),
                    Margin = new Thickness(2, 1, 2, 1),
                    Background = Brushes.Transparent,
                    Cursor = Cursors.Hand,
                    Child = grid,
                    Tag = row.Key
                };
                item.MouseEnter += delegate
                {
                    if ((string)item.Tag != SelectedKey) item.Background = Palette.Well;
                };
                item.MouseLeave += delegate
                {
                    item.Background = (string)item.Tag == SelectedKey ? Palette.Pick : Brushes.Transparent;
                };
                item.MouseLeftButtonUp += delegate
                {
                    SelectedKey = (string)item.Tag;
                    foreach (Border other in stack.Children)
                        other.Background = (string)other.Tag == SelectedKey ? Palette.Pick : Brushes.Transparent;
                    if (Picked != null) Picked(SelectedKey);
                };
                if (row.Key == keep)
                {
                    SelectedKey = keep;
                    item.Background = Palette.Pick;
                }
                stack.Children.Add(item);
            }
        }
    }

    sealed class DeviceLink
    {
        readonly object gate = new object();
        SerialPort port;
        readonly StringBuilder buffer = new StringBuilder();
        public bool IsOpen { get { lock (gate) return port != null && port.IsOpen; } }
        public void Open(string name)
        {
            lock (gate)
            {
                CloseUnlocked();
                var next = new SerialPort(name, 115200) { Encoding = Encoding.UTF8, NewLine = "\n", ReadTimeout = 200, WriteTimeout = 1500, DtrEnable = false, RtsEnable = false };
                next.Open();
                next.DtrEnable = false;
                next.RtsEnable = false;
                port = next;
                buffer.Length = 0;
            }
        }
        public void Close() { lock (gate) CloseUnlocked(); }
        void CloseUnlocked()
        {
            if (port == null) return;
            try { if (port.IsOpen) port.Close(); } catch { }
            port.Dispose();
            port = null;
        }
        public void WriteJson(object command)
        {
            lock (gate)
            {
                if (port == null || !port.IsOpen) throw new InvalidOperationException("请先连接设备。");
                port.WriteLine(Json.Stringify(command));
            }
        }
        public List<string> ReadLines()
        {
            var lines = new List<string>();
            lock (gate)
            {
                if (port == null || !port.IsOpen) return lines;
                var chunk = port.ReadExisting();
                if (string.IsNullOrEmpty(chunk)) return lines;
                buffer.Append(chunk);
                if (buffer.Length > 65536) buffer.Remove(0, buffer.Length - 32768);
                for (;;)
                {
                    var text = buffer.ToString();
                    var at = text.IndexOf('\n');
                    if (at < 0) break;
                    lines.Add(text.Substring(0, at).Trim('\r'));
                    buffer.Remove(0, at + 1);
                }
            }
            return lines;
        }
        public void Present(byte[] frame)
        {
            const int raw = 360;
            lock (gate)
            {
                if (port == null || !port.IsOpen) return;
                for (int offset = 0; offset < frame.Length; offset += raw)
                {
                    int n = Math.Min(raw, frame.Length - offset);
                    var slice = new byte[n];
                    Buffer.BlockCopy(frame, offset, slice, 0, n);
                    var body = new Dictionary<string, object>();
                    body["offset"] = offset;
                    body["data"] = Convert.ToBase64String(slice);
                    body["flush"] = offset + n >= frame.Length;
                    port.WriteLine(Json.Stringify(new Dictionary<string, object> { { "present", body } }));
                }
            }
        }
        public void ClearScreen()
        {
            lock (gate)
            {
                if (port == null || !port.IsOpen) return;
                port.WriteLine(Json.Stringify(new Dictionary<string, object> { { "present", new Dictionary<string, object> { { "clear", true } } } }));
            }
        }
        public bool Capture(string name, string word)
        {
            var deadline = DateTime.UtcNow.AddSeconds(8);
            while (DateTime.UtcNow < deadline)
            {
                SerialPort probe = null;
                try
                {
                    probe = new SerialPort(name, 115200) { Encoding = Encoding.UTF8, NewLine = "\n", ReadTimeout = 50, WriteTimeout = 200, DtrEnable = false, RtsEnable = false };
                    probe.Open();
                    probe.DtrEnable = false;
                    probe.RtsEnable = true;
                    Thread.Sleep(100);
                    probe.RtsEnable = false;
                    var text = "";
                    var until = DateTime.UtcNow.AddMilliseconds(2200);
                    while (DateTime.UtcNow < until)
                    {
                        try { probe.WriteLine(word); } catch { break; }
                        Thread.Sleep(30);
                        try { text += probe.ReadExisting(); } catch { }
                        if (text.Contains("APP_HELD") || text.Contains("APP_RUN")) return true;
                    }
                }
                catch { Thread.Sleep(120); }
                finally
                {
                    if (probe != null)
                    {
                        try { if (probe.IsOpen) probe.Close(); } catch { }
                        probe.Dispose();
                    }
                }
            }
            return false;
        }
    }

    sealed class FramePump
    {
        readonly DeviceLink link;
        readonly object queue = new object();
        byte[] pending;
        bool clear;
        bool alive = true;
        string ip = "";
        DateTime wifiRetry = DateTime.MinValue;
        public FramePump(DeviceLink link)
        {
            this.link = link;
            var thread = new Thread(Loop) { IsBackground = true, Name = "holo-frame" };
            thread.Start();
        }
        public void SetIp(string value) { ip = value ?? ""; }
        public void Submit(byte[] frame)
        {
            lock (queue) pending = frame;
        }
        public void Clear()
        {
            lock (queue) { pending = null; clear = true; }
        }
        void Loop()
        {
            while (alive)
            {
                byte[] frame = null;
                var doClear = false;
                lock (queue)
                {
                    frame = pending;
                    pending = null;
                    doClear = clear;
                    clear = false;
                }
                if (doClear)
                {
                    TryClear();
                    Thread.Sleep(40);
                    continue;
                }
                if (frame == null)
                {
                    Thread.Sleep(30);
                    continue;
                }
                if (!TryWifi(frame, false))
                {
                    try { link.Present(frame); } catch { }
                }
            }
        }
        bool TryWifi(byte[] frame, bool clearing)
        {
            var host = ip;
            if (string.IsNullOrEmpty(host) || DateTime.UtcNow < wifiRetry) return false;
            try
            {
                var url = clearing ? "http://" + host + "/api/present/clear" : "http://" + host + "/api/present";
                var request = (HttpWebRequest)WebRequest.Create(url);
                request.Method = "POST";
                request.Timeout = 800;
                request.ReadWriteTimeout = 800;
                request.Proxy = null;
                if (clearing || frame == null)
                {
                    request.ContentLength = 0;
                }
                else
                {
                    request.ContentType = "application/octet-stream";
                    request.ContentLength = frame.Length;
                    using (var stream = request.GetRequestStream()) stream.Write(frame, 0, frame.Length);
                }
                using (var response = request.GetResponse()) { }
                return true;
            }
            catch
            {
                wifiRetry = DateTime.UtcNow.AddSeconds(4);
                return false;
            }
        }
        void TryClear()
        {
            if (!TryWifi(null, true))
            {
                try { link.ClearScreen(); } catch { }
            }
        }
    }

    sealed class NoticeWindow : Window
    {
        readonly FramePump pump;
        readonly string mode;
        readonly string source;
        readonly Canvas sky = new Canvas { IsHitTestVisible = false };
        readonly List<Spark> sparks = new List<Spark>();
        readonly DispatcherTimer paint = new DispatcherTimer();
        readonly DispatcherTimer push = new DispatcherTimer();
        int tick;
        bool closing;
        public NoticeWindow(FramePump pump, string mode, string source)
        {
            this.pump = pump;
            this.mode = mode;
            this.source = source;
            Width = 420;
            Height = 248;
            WindowStyle = WindowStyle.None;
            AllowsTransparency = true;
            ResizeMode = ResizeMode.NoResize;
            ShowInTaskbar = false;
            Topmost = true;
            Background = Brushes.Transparent;
            var done = mode == "done";
            var name = source == "codex" ? "Codex" : "Cursor";
            var shell = new Border
            {
                Background = done ? ColorBrush(0x0C, 0x0C, 0x10) : ColorBrush(0x1C, 0x14, 0x0C),
                CornerRadius = new CornerRadius(22),
                BorderBrush = ColorBrush(0xFF, 0xFF, 0xFF, 0x18),
                BorderThickness = new Thickness(1),
                ClipToBounds = true
            };
            var root = new Grid();
            root.Children.Add(sky);
            var body = new StackPanel { Margin = new Thickness(28, 26, 28, 22), VerticalAlignment = VerticalAlignment.Bottom };
            body.Children.Add(new TextBlock
            {
                Text = done ? "已经完成" : "需要确认",
                Foreground = Brushes.White,
                FontFamily = Palette.Face,
                FontSize = 28,
                FontWeight = FontWeights.SemiBold
            });
            body.Children.Add(new TextBlock
            {
                Text = done ? name + " 这一轮已经做完。打开它看结果即可。" : "打开 " + name + " 即可继续。这次操作会一直等你。",
                Foreground = ColorBrush(0xD8, 0xD8, 0xDE),
                FontFamily = Palette.Face,
                FontSize = 13,
                TextWrapping = TextWrapping.Wrap,
                Margin = new Thickness(0, 6, 0, 16)
            });
            var buttons = new StackPanel { Orientation = Orientation.Horizontal };
            buttons.Children.Add(Pill("打开 " + name, true, OpenAgent));
            buttons.Children.Add(Pill("关闭", false, CloseNotice));
            body.Children.Add(buttons);
            root.Children.Add(body);
            shell.Child = root;
            Content = shell;
            var area = SystemParameters.WorkArea;
            Left = area.Right - Width - 28;
            Top = area.Bottom - Height - 28;
            paint.Interval = TimeSpan.FromMilliseconds(16);
            paint.Tick += delegate { Step(); };
            paint.Start();
            push.Interval = TimeSpan.FromMilliseconds(120);
            push.Tick += delegate { if (!closing) pump.Submit(RenderScreen()); };
            push.Start();
            if (done) Launch(210, 70);
            Closed += delegate { paint.Stop(); push.Stop(); };
        }
        void OpenAgent()
        {
            try
            {
                var path = FindAgent(source);
                if (!string.IsNullOrEmpty(path)) Process.Start(path);
            }
            catch { }
            CloseNotice();
        }
        void CloseNotice()
        {
            if (closing) return;
            closing = true;
            pump.Clear();
            Close();
        }
        void Launch(double x, double y)
        {
            var colors = new[] { Color.FromRgb(255, 214, 90), Color.FromRgb(255, 255, 255), Color.FromRgb(90, 220, 190), Color.FromRgb(120, 180, 255), Color.FromRgb(255, 140, 160) };
            var random = new Random();
            for (var i = 0; i < 28; i++)
            {
                var angle = random.NextDouble() * Math.PI * 2;
                var speed = 1.2 + random.NextDouble() * 2.4;
                var dot = new Ellipse { Width = 4, Height = 4, Fill = new SolidColorBrush(colors[i % colors.Length]) };
                sky.Children.Add(dot);
                sparks.Add(new Spark { Dot = dot, X = x, Y = y, Vx = Math.Cos(angle) * speed, Vy = Math.Sin(angle) * speed - 1.2, Life = 1 });
            }
        }
        void Step()
        {
            tick++;
            if (mode == "done" && (tick == 18 || tick == 40)) Launch(120 + (tick % 2) * 160, 54);
            foreach (var spark in sparks)
            {
                if (spark.Life <= 0) continue;
                spark.X += spark.Vx;
                spark.Y += spark.Vy;
                spark.Vy += 0.045;
                spark.Life -= 0.012;
                Canvas.SetLeft(spark.Dot, spark.X);
                Canvas.SetTop(spark.Dot, spark.Y);
                spark.Dot.Opacity = Math.Max(0, spark.Life);
            }
        }
        byte[] RenderScreen()
        {
            var visual = new DrawingVisual();
            using (var dc = visual.RenderOpen())
            {
                var done = mode == "done";
                dc.DrawRectangle(done ? new SolidColorBrush(Color.FromRgb(10, 12, 18)) : new SolidColorBrush(Color.FromRgb(28, 20, 10)), null, new Rect(0, 0, 284, 76));
                if (done)
                {
                    var random = new Random(tick / 2 + 3);
                    for (var i = 0; i < 18; i++)
                    {
                        var x = 20 + random.Next(244);
                        var y = 8 + random.Next(40);
                        var radius = 1.2 + (i % 3);
                        dc.DrawEllipse(new SolidColorBrush(Color.FromRgb(255, (byte)(190 + i * 2), 96)), null, new Point(x, y), radius, radius);
                    }
                }
                else
                {
                    var pulse = 0.45 + 0.55 * Math.Abs(Math.Sin(tick / 8.0));
                    dc.DrawRoundedRectangle(new SolidColorBrush(Color.FromArgb((byte)(40 + 80 * pulse), 255, 159, 10)), null, new Rect(8, 8, 268, 60), 14, 14);
                }
                var title = new FormattedText(done ? "已完成" : "请确认", CultureInfo.CurrentUICulture, FlowDirection.LeftToRight, new Typeface(new FontFamily("Microsoft YaHei UI"), FontStyles.Normal, FontWeights.SemiBold, FontStretches.Normal), 26, Brushes.White, 1.25);
                dc.DrawText(title, new Point(16, 10));
                var sub = new FormattedText(source == "codex" ? "打开 Codex" : "打开 Cursor", CultureInfo.CurrentUICulture, FlowDirection.LeftToRight, new Typeface("Microsoft YaHei UI"), 13, new SolidColorBrush(done ? Color.FromRgb(110, 220, 190) : Color.FromRgb(255, 196, 92)), 1.25);
                dc.DrawText(sub, new Point(18, 44));
            }
            var bitmap = new RenderTargetBitmap(284, 76, 96, 96, PixelFormats.Pbgra32);
            bitmap.Render(visual);
            var pixels = new byte[284 * 76 * 4];
            bitmap.CopyPixels(pixels, 284 * 4, 0);
            var raw = new byte[284 * 76 * 2];
            for (int i = 0, o = 0; i < pixels.Length; i += 4, o += 2)
            {
                int r = pixels[i + 2], g = pixels[i + 1], b = pixels[i];
                int value = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
                raw[o] = (byte)(value & 255);
                raw[o + 1] = (byte)(value >> 8);
            }
            return raw;
        }
        static string FindAgent(string source)
        {
            if (source == "codex")
            {
                foreach (var process in Process.GetProcesses())
                {
                    try
                    {
                        if (process.ProcessName.Equals("codex", StringComparison.OrdinalIgnoreCase) && process.MainModule != null)
                            return process.MainModule.FileName;
                    }
                    catch { }
                }
                var root = IoPath.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "OpenAI", "Codex", "bin");
                if (Directory.Exists(root))
                {
                    var found = Directory.GetFiles(root, "codex.exe", SearchOption.AllDirectories);
                    if (found.Length > 0) return found[0];
                }
                return null;
            }
            var cursor = IoPath.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Programs", "cursor", "Cursor.exe");
            return File.Exists(cursor) ? cursor : null;
        }
        static SolidColorBrush ColorBrush(byte r, byte g, byte b, byte a)
        {
            var brush = new SolidColorBrush(Color.FromArgb(a, r, g, b));
            brush.Freeze();
            return brush;
        }
        static SolidColorBrush ColorBrush(byte r, byte g, byte b) { return ColorBrush(r, g, b, 255); }
        static Border Pill(string text, bool primary, Action click)
        {
            var label = new TextBlock { Text = text, Foreground = primary ? Brushes.White : Brushes.White, FontFamily = Palette.Face, FontSize = 13, FontWeight = FontWeights.Medium };
            var pill = new Border
            {
                Background = primary ? Palette.Blue : ColorBrush(0xFF, 0xFF, 0xFF, 0x22),
                CornerRadius = new CornerRadius(16),
                Padding = new Thickness(16, 8, 16, 8),
                Margin = new Thickness(0, 0, 8, 0),
                Cursor = Cursors.Hand,
                Child = label
            };
            pill.MouseEnter += delegate { pill.Opacity = 0.88; };
            pill.MouseLeave += delegate { pill.Opacity = 1; };
            pill.MouseLeftButtonUp += delegate { click(); };
            return pill;
        }
        sealed class Spark
        {
            public Ellipse Dot;
            public double X, Y, Vx, Vy, Life;
        }
    }

    sealed class Shell : Window
    {
        readonly DeviceLink link = new DeviceLink();
        readonly FramePump pump;
        readonly StringBuilder log = new StringBuilder();
        readonly DispatcherTimer timer = new DispatcherTimer();
        readonly Dictionary<string, Grid> pages = new Dictionary<string, Grid>();
        readonly List<Border> nav = new List<Border>();
        ComboBox ports;
        TextBlock connectLabel;
        TextBlock heroTitle;
        TextBlock heroDetail;
        Ellipse heroDot;
        readonly Dictionary<string, TextBlock> watts = new Dictionary<string, TextBlock>();
        readonly Dictionary<string, TextBlock> subs = new Dictionary<string, TextBlock>();
        PickList nearby;
        PickList saved;
        PickList ble;
        TextBox ssid;
        PasswordBox secret;
        TextBox revealed;
        bool showPassword;
        ComboBox locationMode;
        TextBox city, lat, lon, zone;
        TextBlock weatherText;
        TextBlock note;
        TextBlock alertTitle;
        TextBlock alertBody;
        string deviceIp = "";
        string logFail = "";
        bool firmwareKeepsPassword;
        string savedPassword = "";
        DateTime lastQuery = DateTime.MinValue;
        string alertSeen = "";
        NoticeWindow notice;
        public Shell()
        {
            pump = new FramePump(link);
            Title = "HoloStation";
            Width = 1080;
            Height = 740;
            WindowStartupLocation = WindowStartupLocation.CenterScreen;
            WindowStyle = WindowStyle.None;
            ResizeMode = ResizeMode.NoResize;
            Background = Palette.Canvas;
            FontFamily = Palette.Face;
            UseLayoutRounding = true;
            SnapsToDevicePixels = true;
            TextOptions.SetTextFormattingMode(this, TextFormattingMode.Display);
            TextOptions.SetTextRenderingMode(this, TextRenderingMode.ClearType);
            var root = new Grid();
            root.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(252) });
            root.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            root.Children.Add(BuildSide());
            var content = BuildContent();
            Grid.SetColumn(content, 1);
            root.Children.Add(content);
            Content = new Border
            {
                Background = Palette.Canvas,
                BorderBrush = ColorBrush(0, 0, 0, 28),
                BorderThickness = new Thickness(1),
                Child = root
            };
            ShowPage("overview");
            FillPorts();
            timer.Interval = TimeSpan.FromMilliseconds(200);
            timer.Tick += Tick;
            timer.Start();
            Closed += delegate { timer.Stop(); link.Close(); if (notice != null) notice.Close(); };
            Loaded += delegate { if (ports.SelectedItem as string == "COM7") ToggleConnect(); };
        }
        UIElement BuildSide()
        {
            var side = new Grid { Background = Palette.Side };
            side.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            side.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
            side.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            var head = new StackPanel { Margin = new Thickness(22, 22, 22, 8) };
            var lights = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 0, 0, 18) };
            lights.Children.Add(Light(Color.FromRgb(255, 95, 87), delegate { Close(); }));
            lights.Children.Add(Light(Color.FromRgb(254, 188, 46), delegate { WindowState = WindowState.Minimized; }));
            lights.Children.Add(Light(Color.FromRgb(40, 200, 64), delegate { }));
            head.Children.Add(lights);
            head.Children.Add(new TextBlock { Text = "HoloStation", FontSize = 20, FontWeight = FontWeights.SemiBold, Foreground = Palette.Ink });
            head.Children.Add(new TextBlock { Text = "充电与天气", FontSize = 12, Foreground = Palette.Muted, Margin = new Thickness(0, 2, 0, 0) });
            head.MouseLeftButtonDown += delegate { DragMove(); };
            side.Children.Add(head);
            var menu = new StackPanel { Margin = new Thickness(12, 18, 12, 0) };
            menu.Children.Add(Nav("概览", "overview"));
            menu.Children.Add(Nav("无线局域网", "wifi"));
            menu.Children.Add(Nav("蓝牙", "ble"));
            menu.Children.Add(Nav("天气", "weather"));
            menu.Children.Add(Nav("屏幕", "screen"));
            Grid.SetRow(menu, 1);
            side.Children.Add(menu);
            var dock = new StackPanel { Margin = new Thickness(16, 0, 16, 20) };
            ports = new ComboBox { Height = 36, FontSize = 13, Margin = new Thickness(0, 0, 0, 8), Background = Brushes.White, Foreground = Palette.Ink, BorderThickness = new Thickness(0) };
            Chrome.ApplyCombo(ports);
            dock.Children.Add(ports);
            var row = new Grid();
            row.ColumnDefinitions.Add(new ColumnDefinition());
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(8) });
            row.ColumnDefinitions.Add(new ColumnDefinition());
            var connect = Pill("连接", true, ToggleConnect);
            connect.HorizontalAlignment = HorizontalAlignment.Stretch;
            connect.Margin = new Thickness(0);
            connectLabel = (TextBlock)connect.Child;
            var refresh = Pill("刷新", false, FillPorts);
            refresh.HorizontalAlignment = HorizontalAlignment.Stretch;
            refresh.Margin = new Thickness(0);
            Grid.SetColumn(refresh, 2);
            row.Children.Add(connect);
            row.Children.Add(refresh);
            dock.Children.Add(row);
            Grid.SetRow(dock, 2);
            side.Children.Add(dock);
            return side;
        }
        UIElement BuildContent()
        {
            var host = new Grid { Margin = new Thickness(8, 0, 0, 0) };
            host.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
            host.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            var deck = new Grid { Margin = new Thickness(28, 28, 32, 8) };
            deck.Children.Add(Page("overview", Overview()));
            deck.Children.Add(Page("wifi", Wifi()));
            deck.Children.Add(Page("ble", Bluetooth()));
            deck.Children.Add(Page("weather", Weather()));
            deck.Children.Add(Page("screen", ScreenPage()));
            host.Children.Add(deck);
            note = new TextBlock { Text = "密码只保存在设备里。确认和完成会在这台电脑上弹出，并经 Wi-Fi 把画面送到设备屏幕。", Foreground = Palette.Muted, FontSize = 12, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(36, 0, 36, 18) };
            Grid.SetRow(note, 1);
            host.Children.Add(note);
            return host;
        }
        Grid Overview()
        {
            var page = new Grid();
            page.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            page.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            page.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            var hero = Card();
            hero.Padding = new Thickness(24, 20, 24, 20);
            var heroGrid = new Grid();
            heroDot = new Ellipse { Width = 10, Height = 10, Fill = Palette.Hair, Margin = new Thickness(0, 8, 0, 0), VerticalAlignment = VerticalAlignment.Top, HorizontalAlignment = HorizontalAlignment.Left };
            var text = new StackPanel { Margin = new Thickness(20, 0, 0, 0) };
            heroTitle = new TextBlock { Text = "未连接", FontSize = 28, FontWeight = FontWeights.SemiBold, Foreground = Palette.Ink };
            heroDetail = new TextBlock { Text = "用 USB 接上后会自动连接，并保持这个窗口开着。", FontSize = 13, Foreground = Palette.Muted, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 6, 0, 0) };
            text.Children.Add(heroTitle);
            text.Children.Add(heroDetail);
            heroGrid.Children.Add(heroDot);
            heroGrid.Children.Add(text);
            hero.Child = heroGrid;
            page.Children.Add(hero);
            var metrics = Card();
            metrics.Margin = new Thickness(0, 16, 0, 0);
            metrics.Padding = new Thickness(8, 18, 8, 18);
            var metricRow = new UniformGrid { Columns = 4 };
            foreach (var name in new[] { "A1", "C1", "C2", "C3" })
            {
                var cell = new StackPanel { Margin = new Thickness(16, 0, 16, 0) };
                cell.Children.Add(new TextBlock { Text = name, FontSize = 12, Foreground = Palette.Muted });
                var watt = new TextBlock { Text = "--", FontSize = 26, FontWeight = FontWeights.SemiBold, Foreground = Palette.Ink, Margin = new Thickness(0, 6, 0, 0) };
                var sub = new TextBlock { Text = "未检测", FontSize = 12, Foreground = Palette.Muted, Margin = new Thickness(0, 4, 0, 0) };
                watts[name] = watt;
                subs[name] = sub;
                cell.Children.Add(watt);
                cell.Children.Add(sub);
                metricRow.Children.Add(cell);
            }
            metrics.Child = metricRow;
            Grid.SetRow(metrics, 1);
            page.Children.Add(metrics);
            var alert = Card();
            alert.Margin = new Thickness(0, 16, 0, 0);
            alert.Padding = new Thickness(24, 18, 24, 18);
            alertTitle = new TextBlock { Text = "空闲", FontSize = 20, FontWeight = FontWeights.SemiBold, Foreground = Palette.Ink };
            alertBody = new TextBlock { Text = "Cursor 或 Codex 需要确认、或这一轮做完时，会在右下角弹出，并把画面送到屏幕。", FontSize = 13, Foreground = Palette.Muted, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 6, 0, 0) };
            var alertStack = new StackPanel();
            alertStack.Children.Add(alertTitle);
            alertStack.Children.Add(alertBody);
            alert.Child = alertStack;
            Grid.SetRow(alert, 2);
            page.Children.Add(alert);
            return page;
        }
        Grid Wifi()
        {
            var page = new Grid();
            page.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            page.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            page.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            page.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            page.Children.Add(Heading("无线局域网", "最多记住 5 个。点选只填名称，保存后才切换。开机自动连接信号最好的一个。"));
            var scan = Pill("扫描附近网络", false, delegate { Run(delegate { link.WriteJson(new Dictionary<string, object> { { "scanWiFi", new Dictionary<string, object>() } }); Note("正在扫描附近的 2.4 GHz 网络。", "good"); }); });
            scan.Margin = new Thickness(0, 16, 0, 0);
            scan.HorizontalAlignment = HorizontalAlignment.Left;
            Grid.SetRow(scan, 1);
            page.Children.Add(scan);
            var lists = new Grid { Margin = new Thickness(0, 16, 0, 0) };
            lists.ColumnDefinitions.Add(new ColumnDefinition());
            lists.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(16) });
            lists.ColumnDefinitions.Add(new ColumnDefinition());
            nearby = new PickList { Height = 196 };
            saved = new PickList { Height = 196 };
            nearby.Picked += delegate(string key) { ssid.Text = key; };
            saved.Picked += delegate(string key) { ssid.Text = key; };
            var left = Labeled("附近网络", nearby);
            var right = Labeled("已记住", saved);
            Grid.SetColumn(right, 2);
            lists.Children.Add(left);
            lists.Children.Add(right);
            Grid.SetRow(lists, 2);
            page.Children.Add(lists);
            var form = new StackPanel { Margin = new Thickness(0, 8, 0, 0) };
            ssid = new TextBox();
            secret = new PasswordBox { PasswordChar = '•', BorderThickness = new Thickness(0), Background = Brushes.Transparent, FontSize = 14, VerticalAlignment = VerticalAlignment.Center, CaretBrush = Palette.Ink };
            revealed = new TextBox { Visibility = Visibility.Collapsed, BorderThickness = new Thickness(0), Background = Brushes.Transparent, FontSize = 14, Foreground = Palette.Ink, CaretBrush = Palette.Ink, VerticalAlignment = VerticalAlignment.Center };
            var passHost = new Grid();
            passHost.Children.Add(secret);
            passHost.Children.Add(revealed);
            var fields = new Grid { Margin = new Thickness(0, 12, 0, 0) };
            fields.ColumnDefinitions.Add(new ColumnDefinition());
            fields.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(12) });
            fields.ColumnDefinitions.Add(new ColumnDefinition());
            var nameBox = Labeled("名称", Field(ssid));
            var passBox = Labeled("密码", FieldHost(passHost));
            Grid.SetColumn(passBox, 2);
            fields.Children.Add(nameBox);
            fields.Children.Add(passBox);
            form.Children.Add(fields);
            var actions = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 12, 0, 0) };
            actions.Children.Add(Pill("保存并连接", true, SaveWifi));
            actions.Children.Add(Pill("显示密码", false, TogglePassword));
            actions.Children.Add(Pill("打开网页", false, OpenWeb));
            form.Children.Add(actions);
            Grid.SetRow(form, 3);
            page.Children.Add(form);
            return page;
        }
        Grid Bluetooth()
        {
            var page = new Grid();
            page.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            page.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            page.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
            page.Children.Add(Heading("蓝牙", "附近设备，带百分比和信号强度。"));
            var scan = Pill("扫描附近蓝牙", true, delegate { Run(delegate { link.WriteJson(new Dictionary<string, object> { { "scanBle", new Dictionary<string, object>() } }); Note("正在扫描附近蓝牙，大约 3 秒。", "good"); }); });
            scan.Margin = new Thickness(0, 16, 0, 0);
            Grid.SetRow(scan, 1);
            page.Children.Add(scan);
            ble = new PickList { Margin = new Thickness(0, 16, 0, 0) };
            Grid.SetRow(ble, 2);
            page.Children.Add(ble);
            return page;
        }
        Grid Weather()
        {
            var page = new StackPanel();
            page.Children.Add(Heading("天气", "默认按公网 IP 自适应，语言为中文。"));
            var card = Card();
            card.Margin = new Thickness(0, 18, 0, 0);
            card.Padding = new Thickness(20);
            var stack = new StackPanel();
            locationMode = new ComboBox { Height = 36, FontSize = 13, Width = 240, HorizontalAlignment = HorizontalAlignment.Left, Background = Brushes.White, Foreground = Palette.Ink, BorderThickness = new Thickness(0) };
            Chrome.ApplyCombo(locationMode);
            locationMode.Items.Add("自适应（公网 IP）");
            locationMode.Items.Add("自定义位置");
            locationMode.SelectedIndex = 0;
            stack.Children.Add(locationMode);
            city = new TextBox();
            lat = new TextBox();
            lon = new TextBox();
            zone = new TextBox();
            var grid = new Grid { Margin = new Thickness(0, 14, 0, 0) };
            grid.ColumnDefinitions.Add(new ColumnDefinition());
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(12) });
            grid.ColumnDefinitions.Add(new ColumnDefinition());
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(12) });
            grid.ColumnDefinitions.Add(new ColumnDefinition());
            var cityBox = Labeled("城市", Field(city));
            var latBox = Labeled("纬度", Field(lat));
            var lonBox = Labeled("经度", Field(lon));
            Grid.SetColumn(latBox, 2);
            Grid.SetColumn(lonBox, 4);
            grid.Children.Add(cityBox);
            grid.Children.Add(latBox);
            grid.Children.Add(lonBox);
            stack.Children.Add(grid);
            stack.Children.Add(Labeled("时区", Field(zone)));
            var actions = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 14, 0, 0) };
            actions.Children.Add(Pill("保存位置", true, SaveLocation));
            actions.Children.Add(Pill("刷新天气", false, delegate { Run(delegate { link.WriteJson(new Dictionary<string, object> { { "refreshWeather", new Dictionary<string, object>() } }); }); }));
            stack.Children.Add(actions);
            card.Child = stack;
            page.Children.Add(card);
            weatherText = new TextBlock { Text = "等待设备连接。", FontSize = 15, Foreground = Palette.Ink, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(4, 16, 0, 0) };
            page.Children.Add(weatherText);
            var wrap = new Grid();
            wrap.Children.Add(page);
            return wrap;
        }
        Grid ScreenPage()
        {
            var page = new StackPanel();
            page.Children.Add(Heading("屏幕", "选择设备现在显示的页面。"));
            var pagesRow = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 18, 0, 0) };
            pagesRow.Children.Add(Pill("自动轮播", false, delegate { Page(0); }));
            pagesRow.Children.Add(Pill("时钟", false, delegate { Page(2); }));
            pagesRow.Children.Add(Pill("天气", false, delegate { Page(3); }));
            pagesRow.Children.Add(Pill("充电", false, delegate { Page(1); }));
            page.Children.Add(pagesRow);
            var card = Card();
            card.Margin = new Thickness(0, 18, 0, 0);
            card.Padding = new Thickness(22);
            var stack = new StackPanel();
            stack.Children.Add(new TextBlock { Text = "维护", FontSize = 20, FontWeight = FontWeights.SemiBold, Foreground = Palette.Ink });
            stack.Children.Add(new TextBlock { Text = "停止应用会在重启间隙抓住设备，方便串口保持连接。恢复运行会重新启动 Wi-Fi 和天气。", FontSize = 13, Foreground = Palette.Muted, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 6, 0, 14) });
            var actions = new StackPanel { Orientation = Orientation.Horizontal };
            actions.Children.Add(Pill("停止应用", false, StopApp));
            actions.Children.Add(Pill("恢复运行", true, RunApp));
            stack.Children.Add(actions);
            card.Child = stack;
            page.Children.Add(card);
            var wrap = new Grid();
            wrap.Children.Add(page);
            return wrap;
        }
        void Page(int value)
        {
            Run(delegate { link.WriteJson(new Dictionary<string, object> { { "setPage", new Dictionary<string, object> { { "value", value } } } }); });
        }
        void ToggleConnect()
        {
            Run(delegate
            {
                if (link.IsOpen)
                {
                    link.Close();
                    connectLabel.Text = "连接";
                    heroTitle.Text = "未连接";
                    heroDetail.Text = "用 USB 接上后会自动连接，并保持这个窗口开着。";
                    heroDot.Fill = Palette.Hair;
                    Note("已断开 USB。", "");
                    return;
                }
                var name = ports.SelectedItem as string;
                if (string.IsNullOrEmpty(name)) throw new InvalidOperationException("没有检测到串口，请检查 USB 数据线。");
                link.Open(name);
                connectLabel.Text = "断开";
                heroTitle.Text = "正在读取";
                heroDetail.Text = "USB 已连接。";
                heroDot.Fill = Palette.Green;
                lastQuery = DateTime.MinValue;
                link.WriteJson(new Dictionary<string, object> { { "getStatus", new Dictionary<string, object>() } });
                lastQuery = DateTime.UtcNow;
            });
        }
        void FillPorts()
        {
            var keep = ports.SelectedItem as string;
            ports.Items.Clear();
            foreach (var name in SerialPort.GetPortNames()) ports.Items.Add(name);
            if (!string.IsNullOrEmpty(keep) && ports.Items.Contains(keep)) ports.SelectedItem = keep;
            else if (ports.Items.Contains("COM7")) ports.SelectedItem = "COM7";
            else if (ports.Items.Contains("COM6")) ports.SelectedItem = "COM6";
            else if (ports.Items.Count > 0) ports.SelectedIndex = 0;
        }
        void SaveWifi()
        {
            Run(delegate
            {
                if (string.IsNullOrEmpty(ssid.Text)) throw new InvalidOperationException("请填写 Wi-Fi 名称。");
                var typed = showPassword ? revealed.Text : secret.Password;
                var send = typed;
                var message = "已保存。设备最多记住 5 个 Wi-Fi，并自动连接信号最好的一个。";
                if (string.IsNullOrEmpty(typed))
                {
                    if (firmwareKeepsPassword) { send = ""; message = "密码留空，设备将使用这个名称已经记住的密码。"; }
                    else if (!string.IsNullOrEmpty(savedPassword)) { send = savedPassword; message = "密码留空，已沿用本窗口刚才保存的密码。"; }
                    else throw new InvalidOperationException("请填写 Wi-Fi 密码。设备确认支持留空沿用原密码之后，再次保存才可以不填。");
                }
                else
                {
                    if (typed.Length < 8) throw new InvalidOperationException("新密码至少 8 位。留空才会沿用原来的密码。");
                    savedPassword = typed;
                }
                link.WriteJson(new Dictionary<string, object> { { "setWiFi", new Dictionary<string, object> { { "ssid", ssid.Text }, { "psw", send } } } });
                secret.Password = "";
                revealed.Text = "";
                Note(message, "good");
            });
        }
        void SaveLocation()
        {
            Run(delegate
            {
                if (locationMode.SelectedIndex == 0)
                {
                    link.WriteJson(new Dictionary<string, object> { { "setLocation", new Dictionary<string, object> { { "mode", "auto" } } } });
                    Note("已改为按公网 IP 自适应定位。", "good");
                    return;
                }
                if (string.IsNullOrEmpty(city.Text.Trim())) throw new InvalidOperationException("自定义位置请填写城市。经纬度可留空，由设备查询。");
                var body = new Dictionary<string, object> { { "mode", "custom" }, { "city", city.Text.Trim() } };
                if (lat.Text.Trim().Length > 0 || lon.Text.Trim().Length > 0 || zone.Text.Trim().Length > 0)
                {
                    double latitude, longitude;
                    if (!double.TryParse(lat.Text, NumberStyles.Float, CultureInfo.InvariantCulture, out latitude) || !double.TryParse(lon.Text, NumberStyles.Float, CultureInfo.InvariantCulture, out longitude))
                        throw new InvalidOperationException("纬度和经度必须是数字，或全部留空让设备按城市查询。");
                    if (string.IsNullOrEmpty(zone.Text.Trim())) throw new InvalidOperationException("填写了经纬度时，请同时填写时区，例如 Asia/Shanghai。");
                    body["latitude"] = latitude;
                    body["longitude"] = longitude;
                    body["timezone"] = zone.Text.Trim();
                }
                link.WriteJson(new Dictionary<string, object> { { "setLocation", body } });
                Note("已发送自定义位置。", "good");
            });
        }
        void TogglePassword()
        {
            showPassword = !showPassword;
            if (showPassword)
            {
                revealed.Text = secret.Password;
                revealed.Visibility = Visibility.Visible;
                secret.Visibility = Visibility.Collapsed;
            }
            else
            {
                secret.Password = revealed.Text;
                secret.Visibility = Visibility.Visible;
                revealed.Visibility = Visibility.Collapsed;
            }
        }
        void OpenWeb()
        {
            var url = string.IsNullOrEmpty(deviceIp) ? "http://192.168.4.1/" : "http://" + deviceIp + "/";
            Process.Start(url);
        }
        void StopApp()
        {
            Run(delegate
            {
                var name = ports.SelectedItem as string;
                if (string.IsNullOrEmpty(name)) throw new InvalidOperationException("请先选择串口。");
                Mouse.OverrideCursor = Cursors.Wait;
                try
                {
                    link.Close();
                    if (!link.Capture(name, "STOP")) throw new InvalidOperationException("没有在重启间隙抓住设备。请确认 USB 还插着，再按一次。");
                    Note("已在重启间隙停住应用。Wi-Fi 不会再启动。", "good");
                }
                finally { Mouse.OverrideCursor = null; }
                ToggleConnect();
            });
        }
        void RunApp()
        {
            Run(delegate
            {
                link.WriteJson(new Dictionary<string, object> { { "runApp", new Dictionary<string, object>() } });
                Note("已发送恢复运行，设备正在重启。", "good");
            });
        }
        void Tick(object sender, EventArgs e)
        {
            WatchAlert();
            if (!link.IsOpen) return;
            try
            {
                foreach (var line in link.ReadLines())
                {
                    if (line.Contains("Wi-Fi disconnected reason"))
                    {
                        var at = line.LastIndexOf(' ');
                        int reason;
                        if (at >= 0 && int.TryParse(line.Substring(at + 1), out reason))
                        {
                            logFail = Reason(reason);
                            Note(logFail, "bad");
                        }
                    }
                    var start = line.IndexOf("HS_REPLY:");
                    if (start >= 0)
                    {
                        try { Apply(Json.Parse(line.Substring(start + 9))); }
                        catch (Exception ex) { Note("状态解析失败：" + ex.Message, "bad"); }
                    }
                }
                if ((DateTime.UtcNow - lastQuery).TotalSeconds >= 5)
                {
                    link.WriteJson(new Dictionary<string, object> { { "getStatus", new Dictionary<string, object>() } });
                    lastQuery = DateTime.UtcNow;
                }
            }
            catch (Exception ex)
            {
                link.Close();
                connectLabel.Text = "连接";
                heroTitle.Text = "通信中断";
                heroDetail.Text = ex.Message;
                heroDot.Fill = Palette.Red;
            }
        }
        void WatchAlert()
        {
            var path = IoPath.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "HoloStation", "alert.json");
            if (!File.Exists(path)) return;
            string text;
            try { text = File.ReadAllText(path); } catch { return; }
            if (text == alertSeen) return;
            alertSeen = text;
            Dictionary<string, object> alert;
            try { alert = Json.Parse(text); } catch { return; }
            var mode = Json.Str(alert, "mode");
            var source = Json.Str(alert, "source");
            if (string.IsNullOrEmpty(source)) source = "cursor";
            var name = source == "codex" ? "Codex" : "Cursor";
            if (mode == "confirm")
            {
                alertTitle.Text = "需要确认";
                alertTitle.Foreground = Palette.Orange;
                alertBody.Text = name + " 正在等你。打开对应软件即可继续。";
                ShowNotice(mode, source);
            }
            else if (mode == "done")
            {
                alertTitle.Text = "已经完成";
                alertTitle.Foreground = Palette.Green;
                alertBody.Text = name + " 这一轮做完了。";
                ShowNotice(mode, source);
            }
            else
            {
                alertTitle.Text = "空闲";
                alertTitle.Foreground = Palette.Ink;
                alertBody.Text = "Cursor 或 Codex 需要确认、或这一轮做完时，会在右下角弹出，并把画面送到设备屏幕。";
                if (notice != null) { notice.Close(); notice = null; pump.Clear(); }
            }
        }
        void ShowNotice(string mode, string source)
        {
            if (notice != null) notice.Close();
            notice = new NoticeWindow(pump, mode, source);
            notice.Closed += delegate { notice = null; };
            notice.Show();
        }
        void Apply(Dictionary<string, object> data)
        {
            try
            {
                File.WriteAllText(IoPath.Combine(AppDomain.CurrentDomain.BaseDirectory, "HoloStation_last_status.json"), Json.Stringify(data), new UTF8Encoding(false));
            }
            catch { }
            if (data.ContainsKey("wifiReason")) firmwareKeepsPassword = true;
            if (data.ContainsKey("error"))
            {
                Note("设备返回：" + Explain(Json.Str(data, "error")), "bad");
                return;
            }
            deviceIp = Json.Str(data, "ip");
            pump.SetIp(deviceIp);
            var config = Json.Map(data.ContainsKey("config") ? data["config"] : null);
            var wifiState = config == null ? 0 : Json.Int(config.ContainsKey("wifiState") ? config["wifiState"] : null);
            var version = config == null ? "" : Json.Str(config, "Ver");
            var fail = Json.Str(data, "wifiFail");
            if (string.IsNullOrEmpty(fail) && wifiState != 2 && data.ContainsKey("wifiReason")) fail = Reason(Json.Int(data["wifiReason"]));
            if (string.IsNullOrEmpty(fail) && wifiState != 2) fail = logFail;
            var ssidText = Json.Str(data, "ssid");
            if (string.IsNullOrEmpty(ssidText)) ssidText = "未连接网络";
            var held = data.ContainsKey("appHeld") && Json.Bool(data["appHeld"]);
            if (held)
            {
                heroTitle.Text = "应用已停止";
                heroDetail.Text = version + "  ·  Wi-Fi 不会启动，需要时在屏幕页恢复运行。";
                heroDot.Fill = Palette.Orange;
            }
            else if (wifiState == 2)
            {
                var signal = data.ContainsKey("wifiSignal") ? "  ·  " + Json.Int(data["wifiSignal"]) + "%  " + Json.Int(data["wifiRssi"]) + " dBm" : "";
                heroTitle.Text = "已连接";
                heroDetail.Text = ssidText + "  ·  " + deviceIp + signal + "  ·  " + version;
                heroDot.Fill = Palette.Green;
            }
            else
            {
                heroTitle.Text = "未联网";
                heroDetail.Text = ssidText + "  ·  " + (string.IsNullOrEmpty(fail) ? "还没有拿到 IP" : fail);
                heroDot.Fill = Palette.Hair;
            }
            var ap = Json.Str(data, "apSsid");
            if (!string.IsNullOrEmpty(ap)) heroDetail.Text += "\n热点 " + ap + "  ·  http://192.168.4.1/";
            foreach (var name in new[] { "A1", "C1", "C2", "C3" })
            {
                var port = Json.Map(data.ContainsKey(name) ? data[name] : null);
                if (port != null && port.ContainsKey("valid") && Json.Bool(port["valid"]))
                {
                    watts[name].Text = Json.Num(port.ContainsKey("P") ? port["P"] : null).ToString("0.0") + " W";
                    subs[name].Text = Json.Num(port.ContainsKey("V") ? port["V"] : null).ToString("0.0") + " V  ·  " + Json.Num(port.ContainsKey("I") ? port["I"] : null).ToString("0.00") + " A";
                }
                else
                {
                    watts[name].Text = "--";
                    subs[name].Text = "未检测";
                }
            }
            var weather = Json.Map(data.ContainsKey("weather") ? data["weather"] : null);
            if (weather != null)
            {
                var mode = Json.Str(weather, "mode");
                if (string.IsNullOrEmpty(mode) && weather.ContainsKey("autoLocation") && Json.Bool(weather["autoLocation"])) mode = "auto";
                if (!locationMode.IsKeyboardFocusWithin)
                    locationMode.SelectedIndex = mode == "custom" ? 1 : 0;
                var cityName = Json.Str(weather, "city");
                if (!city.IsKeyboardFocusWithin && cityName.Length > 0 && cityName != "IP定位") city.Text = cityName;
                if (!zone.IsKeyboardFocusWithin && Json.Str(weather, "timezone").Length > 0) zone.Text = Json.Str(weather, "timezone");
                if (!lat.IsKeyboardFocusWithin && weather.ContainsKey("latitude")) lat.Text = Convert.ToString(weather["latitude"], CultureInfo.InvariantCulture);
                if (!lon.IsKeyboardFocusWithin && weather.ContainsKey("longitude")) lon.Text = Convert.ToString(weather["longitude"], CultureInfo.InvariantCulture);
                var place = mode == "custom" ? "自定义" : "自适应";
                if (weather.ContainsKey("valid") && Json.Bool(weather["valid"]))
                {
                    var suffix = weather.ContainsKey("stale") && Json.Bool(weather["stale"]) ? "  ·  缓存" : "";
                    weatherText.Text = place + "  ·  " + cityName + "\n" + Json.Num(weather.ContainsKey("temperature") ? weather["temperature"] : null).ToString("0.0") + " °C  ·  " + Json.Str(weather, "condition") + "  ·  湿度 " + Json.Int(weather.ContainsKey("humidity") ? weather["humidity"] : null) + "%" + suffix;
                }
                else weatherText.Text = "本地天气（" + place + "）还在定位和校时，首次大约一分钟。";
            }
            if (!string.IsNullOrEmpty(fail) && wifiState != 2) Note(fail, "bad");
            var scan = new Dictionary<string, Dictionary<string, object>>();
            if (data.ContainsKey("wifiScan"))
                foreach (var item in Json.Items(data["wifiScan"]))
                {
                    var map = Json.Map(item);
                    var key = Json.Str(map, "ssid");
                    if (key.Length > 0) scan[key] = map;
                }
            var savedRows = new List<Row>();
            if (data.ContainsKey("wifiSaved"))
                foreach (var item in Json.Items(data["wifiSaved"]))
                {
                    var key = Convert.ToString(item);
                    if (string.IsNullOrEmpty(key)) continue;
                    Dictionary<string, object> hit;
                    var meta = "";
                    if (scan.TryGetValue(key, out hit)) meta = Json.Int(hit["signal"]) + "%   " + Json.Int(hit["rssi"]) + " dBm";
                    savedRows.Add(new Row { Key = key, Title = key, Meta = meta });
                }
            var nearRows = new List<Row>();
            foreach (var pair in scan)
            {
                var open = pair.Value.ContainsKey("open") && Json.Bool(pair.Value["open"]) ? "开放" : "";
                nearRows.Add(new Row { Key = pair.Key, Title = pair.Key, Meta = Json.Int(pair.Value["signal"]) + "%   " + Json.Int(pair.Value["rssi"]) + " dBm  " + open });
            }
            saved.SetItems(savedRows);
            nearby.SetItems(nearRows);
            var bleRows = new List<Row>();
            if (data.ContainsKey("bleScan"))
                foreach (var item in Json.Items(data["bleScan"]))
                {
                    var map = Json.Map(item);
                    var addr = Json.Str(map, "addr");
                    var label = Json.Str(map, "name");
                    if (label.Length == 0) label = addr;
                    bleRows.Add(new Row { Key = addr, Title = label, Meta = Json.Int(map.ContainsKey("signal") ? map["signal"] : null) + "%   " + Json.Int(map.ContainsKey("rssi") ? map["rssi"] : null) + " dBm" });
                }
            ble.SetItems(bleRows);
        }
        void ShowPage(string key)
        {
            foreach (var pair in pages) pair.Value.Visibility = pair.Key == key ? Visibility.Visible : Visibility.Collapsed;
            foreach (var item in nav)
            {
                var on = (string)item.Tag == key;
                item.Background = on ? Brushes.White : Brushes.Transparent;
                item.Effect = on ? new DropShadowEffect { BlurRadius = 12, ShadowDepth = 1, Opacity = 0.08, Direction = 270, Color = Colors.Black } : null;
            }
        }
        void Run(Action action)
        {
            try { action(); }
            catch (Exception ex) { Note(ex.Message, "bad"); }
        }
        void Note(string text, string kind)
        {
            note.Text = text;
            note.Foreground = kind == "bad" ? Palette.Red : kind == "good" ? Palette.Green : Palette.Muted;
        }
        static string Explain(string name)
        {
            if (name == "ESP_ERR_NOT_FOUND") return "这个 Wi-Fi 还没有保存过密码。请填写至少 8 位；开放网络可以留空。";
            if (name == "ESP_ERR_INVALID_ARG") return "参数无效。Wi-Fi 名称 1-32 字；新密码至少 8 位。自定义位置需要城市。";
            if (name == "ESP_ERR_INVALID_STATE") return "设备正忙，或天气服务还没启动。";
            if (name == "ESP_ERR_TIMEOUT") return "扫描超时，请再试一次。";
            if (name == "ESP_ERR_NOT_SUPPORTED") return "当前固件还不认识这条命令，需要烧录这次更新。";
            return name;
        }
        static string Reason(int reason)
        {
            if (reason == 15) return "密码错误或握手超时";
            if (reason == 201) return "找不到这个 Wi-Fi，请确认名称和 2.4 GHz";
            if (reason == 202 || reason == 23) return "认证失败，请核对密码";
            if (reason == 203) return "已找到热点，但关联失败";
            if (reason == 204) return "握手超时，多半是密码错误";
            if (reason == 200) return "信号中断，路由器无响应";
            if (reason == 2) return "认证超时";
            if (reason == 8) return "路由器主动断开了连接";
            if (reason == 0) return "";
            return "联网失败，原因码 " + reason;
        }
        Grid Page(string key, UIElement child)
        {
            var grid = child as Grid ?? new Grid();
            if (!ReferenceEquals(grid, child)) grid.Children.Add(child);
            pages[key] = grid;
            return grid;
        }
        Border Nav(string text, string key)
        {
            var label = new TextBlock { Text = text, FontSize = 13, Foreground = Palette.Ink, VerticalAlignment = VerticalAlignment.Center };
            var item = new Border { CornerRadius = new CornerRadius(10), Padding = new Thickness(14, 0, 14, 0), Height = 36, Margin = new Thickness(0, 2, 0, 2), Cursor = Cursors.Hand, Child = label, Tag = key, Background = Brushes.Transparent };
            item.MouseEnter += delegate { if ((string)item.Tag != CurrentPage()) item.Background = ColorBrush(255, 255, 255, 160); };
            item.MouseLeave += delegate { if ((string)item.Tag != CurrentPage()) item.Background = Brushes.Transparent; };
            item.MouseLeftButtonUp += delegate { ShowPage(key); };
            nav.Add(item);
            return item;
        }
        string CurrentPage()
        {
            foreach (var pair in pages) if (pair.Value.Visibility == Visibility.Visible) return pair.Key;
            return "";
        }
        static Border Card()
        {
            return new Border
            {
                Background = Palette.Card,
                CornerRadius = new CornerRadius(16),
                SnapsToDevicePixels = true,
                Effect = new DropShadowEffect { BlurRadius = 30, ShadowDepth = 0, Opacity = 0.06, Color = Colors.Black, RenderingBias = RenderingBias.Quality }
            };
        }
        static StackPanel Heading(string title, string subtitle)
        {
            var stack = new StackPanel();
            stack.Children.Add(new TextBlock { Text = title, FontSize = 30, FontWeight = FontWeights.SemiBold, Foreground = Palette.Ink });
            stack.Children.Add(new TextBlock { Text = subtitle, FontSize = 13, Foreground = Palette.Muted, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 4, 0, 0) });
            return stack;
        }
        static StackPanel Labeled(string caption, UIElement field)
        {
            var stack = new StackPanel { Margin = new Thickness(0, 8, 0, 0) };
            stack.Children.Add(new TextBlock { Text = caption, FontSize = 12, Foreground = Palette.Muted, Margin = new Thickness(2, 0, 0, 6) });
            stack.Children.Add(field);
            return stack;
        }
        static Border Field(TextBox box)
        {
            box.BorderThickness = new Thickness(0);
            box.Background = Brushes.Transparent;
            box.FontSize = 14;
            box.Foreground = Palette.Ink;
            box.CaretBrush = Palette.Ink;
            box.VerticalAlignment = VerticalAlignment.Center;
            return FieldHost(box);
        }
        static Border FieldHost(UIElement child)
        {
            return new Border { Background = Brushes.White, BorderBrush = ColorBrush(0, 0, 0, 20), BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(10), Padding = new Thickness(12, 6, 12, 6), Height = 40, Child = child };
        }
        static Border Pill(string text, bool primary, Action click)
        {
            var label = new TextBlock { Text = text, FontSize = 13, FontWeight = FontWeights.Medium, Foreground = primary ? Brushes.White : Palette.Ink, HorizontalAlignment = HorizontalAlignment.Center };
            var pill = new Border
            {
                Background = primary ? Palette.Blue : Brushes.White,
                BorderBrush = primary ? null : ColorBrush(0, 0, 0, 22),
                BorderThickness = new Thickness(primary ? 0 : 1),
                CornerRadius = new CornerRadius(16),
                Padding = new Thickness(16, 8, 16, 8),
                Margin = new Thickness(0, 0, 8, 0),
                Cursor = Cursors.Hand,
                HorizontalAlignment = HorizontalAlignment.Left,
                Child = label
            };
            pill.MouseEnter += delegate { pill.Background = primary ? Palette.BlueHot : Palette.Well; };
            pill.MouseLeave += delegate { pill.Background = primary ? Palette.Blue : Brushes.White; };
            pill.MouseLeftButtonUp += delegate { click(); };
            return pill;
        }
        static Ellipse Light(Color color, Action click)
        {
            var dot = new Ellipse { Width = 12, Height = 12, Fill = new SolidColorBrush(color), Margin = new Thickness(0, 0, 8, 0), Cursor = Cursors.Hand };
            dot.MouseLeftButtonDown += delegate(object sender, MouseButtonEventArgs e) { e.Handled = true; click(); };
            return dot;
        }
        static SolidColorBrush ColorBrush(byte r, byte g, byte b, byte a)
        {
            var brush = new SolidColorBrush(Color.FromArgb(a, r, g, b));
            brush.Freeze();
            return brush;
        }
        protected override void OnSourceInitialized(EventArgs e)
        {
            base.OnSourceInitialized(e);
            Chrome.Shadow(this);
        }
    }

    static class Chrome
    {
        const string ComboXaml =
            "<ControlTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml' TargetType='ComboBox'>" +
            "<Grid>" +
            "<ToggleButton Focusable='False' ClickMode='Press' IsChecked='{Binding IsDropDownOpen, RelativeSource={RelativeSource TemplatedParent}, Mode=TwoWay}'>" +
            "<ToggleButton.Template><ControlTemplate TargetType='ToggleButton'>" +
            "<Border Background='{TemplateBinding Background}' CornerRadius='10' Height='36'>" +
            "<Grid><Grid.ColumnDefinitions><ColumnDefinition/><ColumnDefinition Width='32'/></Grid.ColumnDefinitions>" +
            "<ContentPresenter Margin='14,0,0,0' VerticalAlignment='Center' HorizontalAlignment='Left' " +
            "Content='{Binding SelectionBoxItem, RelativeSource={RelativeSource AncestorType=ComboBox}}'/>" +
            "<Path Grid.Column='1' Data='M 1,1 L 5,5 L 9,1' Stroke='#86868B' StrokeThickness='1.4' StrokeStartLineCap='Round' StrokeEndLineCap='Round' StrokeLineJoin='Round' HorizontalAlignment='Center' VerticalAlignment='Center'/>" +
            "</Grid></Border></ControlTemplate></ToggleButton.Template></ToggleButton>" +
            "<Popup Placement='Bottom' AllowsTransparency='True' Focusable='False' PopupAnimation='Fade' IsOpen='{TemplateBinding IsDropDownOpen}'>" +
            "<Border Background='White' CornerRadius='12' Padding='4' Margin='0,6,0,0' MinWidth='{Binding ActualWidth, RelativeSource={RelativeSource TemplatedParent}}'>" +
            "<Border.Effect><DropShadowEffect BlurRadius='24' ShadowDepth='6' Opacity='0.16'/></Border.Effect>" +
            "<ScrollViewer MaxHeight='240'><ItemsPresenter/></ScrollViewer>" +
            "</Border></Popup></Grid></ControlTemplate>";
        const string ItemXaml =
            "<Style xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml' TargetType='ComboBoxItem'>" +
            "<Setter Property='FontSize' Value='13'/><Setter Property='Foreground' Value='#1D1D1F'/>" +
            "<Setter Property='Template'><Setter.Value><ControlTemplate TargetType='ComboBoxItem'>" +
            "<Border x:Name='Bd' Background='Transparent' CornerRadius='8' Padding='12,8'>" +
            "<ContentPresenter/></Border>" +
            "<ControlTemplate.Triggers><Trigger Property='IsHighlighted' Value='True'>" +
            "<Setter TargetName='Bd' Property='Background' Value='#F2F2F7'/></Trigger>" +
            "<Trigger Property='IsSelected' Value='True'><Setter TargetName='Bd' Property='Background' Value='#E8E8ED'/></Trigger>" +
            "</ControlTemplate.Triggers></ControlTemplate></Setter.Value></Setter></Style>";
        static readonly ControlTemplate ComboTemplate = (ControlTemplate)XamlReader.Parse(ComboXaml);
        static readonly Style ComboItem = (Style)XamlReader.Parse(ItemXaml);
        public static void ApplyCombo(ComboBox box)
        {
            box.Template = ComboTemplate;
            box.ItemContainerStyle = ComboItem;
            box.FontFamily = Palette.Face;
        }
        public static void Shadow(Window window)
        {
            var hwnd = new WindowInteropHelper(window).Handle;
            int policy = 2;
            DwmSetWindowAttribute(hwnd, 2, ref policy, 4);
        }
        [DllImport("dwmapi.dll")]
        static extern int DwmSetWindowAttribute(IntPtr hwnd, int attr, ref int value, int size);
        [DllImport("dwmapi.dll")]
        static extern int DwmExtendFrameIntoClientArea(IntPtr hwnd, ref Margins margins);
        [StructLayout(LayoutKind.Sequential)]
        struct Margins { public int Left, Right, Top, Bottom; }
    }

    static class Program
    {
        [STAThread]
        static void Main()
        {
            var log = IoPath.Combine(IoPath.GetTempPath(), "HoloStation-start.txt");
            try
            {
                var app = new Application { ShutdownMode = ShutdownMode.OnMainWindowClose };
                app.DispatcherUnhandledException += delegate(object sender, System.Windows.Threading.DispatcherUnhandledExceptionEventArgs e)
                {
                    File.WriteAllText(log, e.Exception.ToString());
                    e.Handled = true;
                    Shutdown();
                };
                app.Run(new Shell());
            }
            catch (Exception ex)
            {
                File.WriteAllText(log, ex.ToString());
            }
        }
        static void Shutdown()
        {
            if (Application.Current != null) Application.Current.Shutdown();
        }
    }
}
