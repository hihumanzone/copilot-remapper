using System;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using System.Web.Script.Serialization;
using System.Windows.Forms;
using Microsoft.Win32;

namespace CopilotRemapper
{
    public class AppConfig
    {
        public string ActionType { get; set; }
        public string TargetPath { get; set; }
        public string Arguments { get; set; }
        public string WorkingDirectory { get; set; }
        public bool RunAsAdmin { get; set; }
        public bool StartWithWindows { get; set; }
        public int DebounceMs { get; set; }

        public AppConfig()
        {
            ActionType = "LaunchApp";
            TargetPath = "wt.exe";
            Arguments = "";
            WorkingDirectory = "";
            RunAsAdmin = false;
            StartWithWindows = false;
            DebounceMs = 300;
        }
    }

    public static class ConfigManager
    {
        private static readonly string Path = GetConfigPath();
        private static AppConfig _cached;

        private static string GetConfigPath()
        {
            string local = System.IO.Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "copilot_config.json");
            try
            {
                File.WriteAllText(local + ".tmp", "");
                File.Delete(local + ".tmp");
                return local;
            }
            catch
            {
                string appData = System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "CopilotRemapper");
                Directory.CreateDirectory(appData);
                return System.IO.Path.Combine(appData, "copilot_config.json");
            }
        }

        public static AppConfig Load()
        {
            if (_cached != null) return _cached;
            try
            {
                if (File.Exists(Path))
                {
                    var cfg = new JavaScriptSerializer().Deserialize<AppConfig>(File.ReadAllText(Path));
                    if (cfg != null) return _cached = cfg;
                }
            }
            catch { }
            var def = new AppConfig();
            Save(def);
            return _cached = def;
        }

        public static void Save(AppConfig cfg)
        {
            _cached = cfg;
            try { File.WriteAllText(Path, new JavaScriptSerializer().Serialize(cfg)); } catch { }
        }

        public static void SetStartup(bool enable)
        {
            try
            {
                using (var key = Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Run", true))
                {
                    if (key == null) return;
                    if (enable) key.SetValue("CopilotRemapper", "\"" + Application.ExecutablePath + "\" --silent");
                    else if (key.GetValue("CopilotRemapper") != null) key.DeleteValue("CopilotRemapper", false);
                }
            }
            catch { }
        }

        public static bool IsStartupEnabled()
        {
            try
            {
                using (var key = Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Run", false))
                    return key != null && key.GetValue("CopilotRemapper") != null;
            }
            catch { return false; }
        }
    }

    public static class ActionRunner
    {
        [DllImport("user32.dll")]
        private static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);

        public static void MaskStartMenu()
        {
            keybd_event(0xE8, 0, 0, UIntPtr.Zero);
            keybd_event(0xE8, 0, 2, UIntPtr.Zero);
        }

        public static void ReleaseModifiers()
        {
            keybd_event(0xA0, 0, 2, UIntPtr.Zero); // VK_LSHIFT
            keybd_event(0x5B, 0, 2, UIntPtr.Zero); // VK_LWIN
        }

        public static void Execute(AppConfig cfg)
        {
            if (cfg == null || string.IsNullOrEmpty(cfg.TargetPath)) return;

            ThreadPool.QueueUserWorkItem(delegate
            {
                try
                {
                    ReleaseModifiers();
                    var psi = new ProcessStartInfo { UseShellExecute = true };
                    string target = Environment.ExpandEnvironmentVariables(cfg.TargetPath);

                    if (cfg.ActionType == "OpenUrl")
                    {
                        if (!target.Contains("://")) target = "https://" + target;
                        psi.FileName = target;
                    }
                    else if (cfg.ActionType == "RunCommand")
                    {
                        psi.FileName = "powershell.exe";
                        psi.Arguments = "-NoProfile -ExecutionPolicy Bypass -Command \"" + target + "\"";
                        psi.WindowStyle = ProcessWindowStyle.Hidden;
                    }
                    else
                    {
                        psi.FileName = target;
                        psi.Arguments = string.IsNullOrEmpty(cfg.Arguments) ? "" : Environment.ExpandEnvironmentVariables(cfg.Arguments);
                    }

                    if (!string.IsNullOrEmpty(cfg.WorkingDirectory) && Directory.Exists(cfg.WorkingDirectory))
                        psi.WorkingDirectory = Environment.ExpandEnvironmentVariables(cfg.WorkingDirectory);

                    if (cfg.RunAsAdmin && cfg.ActionType != "OpenUrl")
                        psi.Verb = "runas";

                    Process.Start(psi);
                }
                catch { }
            });
        }
    }

    public class KeyboardHook : IDisposable
    {
        [DllImport("user32.dll", SetLastError = true)]
        private static extern IntPtr SetWindowsHookEx(int idHook, HookProc lpfn, IntPtr hMod, uint dwThreadId);
        [DllImport("user32.dll", SetLastError = true)]
        private static extern bool UnhookWindowsHookEx(IntPtr hhk);
        [DllImport("user32.dll")]
        private static extern IntPtr CallNextHookEx(IntPtr hhk, int nCode, IntPtr wParam, IntPtr lParam);
        [DllImport("kernel32.dll")]
        private static extern IntPtr GetModuleHandle(string lpModuleName);

        private delegate IntPtr HookProc(int nCode, IntPtr wParam, IntPtr lParam);
        private readonly HookProc _proc;
        private IntPtr _hookId = IntPtr.Zero;
        private DateTime _lastTrigger = DateTime.MinValue;

        public event EventHandler CopilotKeyDetected;

        public KeyboardHook()
        {
            _proc = HookCallback;
            _hookId = SetWindowsHookEx(13, _proc, GetModuleHandle(null), 0); // WH_KEYBOARD_LL = 13
        }

        private IntPtr HookCallback(int nCode, IntPtr wParam, IntPtr lParam)
        {
            if (nCode >= 0)
            {
                int msg = wParam.ToInt32();
                int vkCode = Marshal.ReadInt32(lParam);

                if (vkCode == 0x86) // VK_F23
                {
                    if (msg == 0x0100 || msg == 0x0104) // WM_KEYDOWN, WM_SYSKEYDOWN
                    {
                        ActionRunner.MaskStartMenu();
                        DateTime now = DateTime.UtcNow;
                        var cfg = ConfigManager.Load();
                        int debounce = cfg.DebounceMs > 50 ? cfg.DebounceMs : 300;

                        if ((now - _lastTrigger).TotalMilliseconds >= debounce)
                        {
                            _lastTrigger = now;
                            ActionRunner.Execute(cfg);
                            var h = CopilotKeyDetected;
                            if (h != null) h(this, EventArgs.Empty);
                        }
                        return (IntPtr)1;
                    }
                    if (msg == 0x0101 || msg == 0x0105) return (IntPtr)1; // WM_KEYUP, WM_SYSKEYUP
                }
            }
            return CallNextHookEx(_hookId, nCode, wParam, lParam);
        }

        public void Dispose()
        {
            if (_hookId != IntPtr.Zero) { UnhookWindowsHookEx(_hookId); _hookId = IntPtr.Zero; }
        }
    }

    public static class NativeTheme
    {
        [DllImport("dwmapi.dll")]
        private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attr, ref int val, int sz);
        [DllImport("user32.dll")]
        public static extern bool DestroyIcon(IntPtr hIcon);

        public static void Apply(IntPtr handle)
        {
            try
            {
                if (Environment.OSVersion.Version.Major >= 10 && Environment.OSVersion.Version.Build >= 17763)
                {
                    int dark = 0;
                    using (var key = Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize", false))
                    {
                        if (key != null)
                        {
                            object val = key.GetValue("AppsUseLightTheme");
                            if (val is int && ((int)val) == 0) dark = 1;
                        }
                    }
                    int attr = Environment.OSVersion.Version.Build >= 18985 ? 20 : 19;
                    DwmSetWindowAttribute(handle, attr, ref dark, sizeof(int));
                }
            }
            catch { }
        }
    }

    public class SettingsForm : Form
    {
        private AppConfig _config;
        private readonly KeyboardHook _hook;

        private ComboBox _comboAction;
        private Label _lblTarget, _lblArgs;
        private TextBox _textTarget, _textArgs;
        private Button _btnBrowse, _btnTest, _btnSave, _btnClose;
        private CheckBox _checkStartup, _checkAdmin;
        private ToolStripStatusLabel _statusLabel;
        private TableLayoutPanel _targetRow;

        public SettingsForm(KeyboardHook hook)
        {
            _hook = hook;
            _config = ConfigManager.Load();
            InitUI();
            LoadConfigToUI();

            if (_hook != null)
            {
                _hook.CopilotKeyDetected += delegate
                {
                    Action update = delegate { _statusLabel.Text = "Copilot key pressed at " + DateTime.Now.ToString("HH:mm:ss") + "! Action executed."; };
                    if (InvokeRequired) BeginInvoke(update);
                    else update();
                };
            }
        }

        protected override void OnHandleCreated(EventArgs e)
        {
            base.OnHandleCreated(e);
            NativeTheme.Apply(this.Handle);
        }

        protected override void WndProc(ref Message m)
        {
            if (m.Msg == 0x001A || m.Msg == 0x031A) NativeTheme.Apply(this.Handle);
            base.WndProc(ref m);
        }

        private void InitUI()
        {
            this.Text = "Copilot Key Remapper";
            this.Icon = TrayAppContext.GetAppIcon();
            this.Font = SystemFonts.MessageBoxFont;
            this.FormBorderStyle = FormBorderStyle.FixedDialog;
            this.MaximizeBox = false;
            this.MinimizeBox = false;
            this.ShowInTaskbar = true;
            this.StartPosition = FormStartPosition.CenterScreen;
            this.AutoScaleMode = AutoScaleMode.Font;
            this.AutoScaleDimensions = new SizeF(7F, 15F);
            this.AutoSize = true;
            this.AutoSizeMode = AutoSizeMode.GrowAndShrink;
            this.MinimumSize = new Size(460, 0);

            // Group 1: Key Action
            var grpAction = new GroupBox { Text = "Key Action", Dock = DockStyle.Top, AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, Padding = new Padding(10, 8, 10, 10), Margin = new Padding(0, 0, 0, 10) };
            var tblAction = new TableLayoutPanel { Dock = DockStyle.Top, AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, ColumnCount = 1 };
            tblAction.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));

            tblAction.Controls.Add(new Label { Text = "&Remap Copilot key to:", AutoSize = true, Margin = new Padding(0, 2, 0, 4) });

            _comboAction = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Dock = DockStyle.Top, TabIndex = 0, Margin = new Padding(0, 0, 0, 8) };
            _comboAction.Items.AddRange(new object[] { "Launch Application / File", "Open Website / URL", "Run PowerShell Command" });
            _comboAction.SelectedIndexChanged += delegate { UpdateTargetPrompt(); };
            tblAction.Controls.Add(_comboAction);

            _lblTarget = new Label { Text = "&Application or file to open:", AutoSize = true, Margin = new Padding(0, 2, 0, 4) };
            tblAction.Controls.Add(_lblTarget);

            _targetRow = new TableLayoutPanel { Dock = DockStyle.Top, AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, ColumnCount = 2, Margin = new Padding(0, 0, 0, 8) };
            _targetRow.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
            _targetRow.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            _textTarget = new TextBox { Dock = DockStyle.Fill, TabIndex = 1 };
            _btnBrowse = new Button { Text = "&Browse...", FlatStyle = FlatStyle.System, AutoSize = true, MinimumSize = new Size(75, 23), Margin = new Padding(6, 0, 0, 0), TabIndex = 2 };
            _btnBrowse.Click += BtnBrowse_Click;
            _targetRow.Controls.Add(_textTarget, 0, 0);
            _targetRow.Controls.Add(_btnBrowse, 1, 0);
            tblAction.Controls.Add(_targetRow);

            _lblArgs = new Label { Text = "A&rguments (optional):", AutoSize = true, Margin = new Padding(0, 2, 0, 4) };
            tblAction.Controls.Add(_lblArgs);

            _textArgs = new TextBox { Dock = DockStyle.Top, TabIndex = 3, Margin = new Padding(0, 0, 0, 2) };
            tblAction.Controls.Add(_textArgs);
            grpAction.Controls.Add(tblAction);

            // Group 2: Options
            var grpOptions = new GroupBox { Text = "Options", Dock = DockStyle.Top, AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, Padding = new Padding(10, 8, 10, 10), Margin = new Padding(0, 0, 0, 10) };
            var tblOptions = new TableLayoutPanel { Dock = DockStyle.Top, AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, ColumnCount = 1 };
            tblOptions.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));

            _checkStartup = new CheckBox { Text = "&Start automatically when Windows starts", FlatStyle = FlatStyle.System, AutoSize = true, TabIndex = 4, Margin = new Padding(0, 2, 0, 4) };
            _checkAdmin = new CheckBox { Text = "Run with &administrator privileges", FlatStyle = FlatStyle.System, AutoSize = true, TabIndex = 5, Margin = new Padding(0, 2, 0, 2) };
            tblOptions.Controls.Add(_checkStartup);
            tblOptions.Controls.Add(_checkAdmin);
            grpOptions.Controls.Add(tblOptions);

            // Buttons Bar
            var tblButtons = new TableLayoutPanel { Dock = DockStyle.Top, AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, ColumnCount = 4, RowCount = 1, Margin = new Padding(0, 4, 0, 0) };
            tblButtons.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            tblButtons.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
            tblButtons.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            tblButtons.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));

            _btnTest = new Button { Text = "&Test", FlatStyle = FlatStyle.System, AutoSize = true, MinimumSize = new Size(75, 23), TabIndex = 6 };
            _btnTest.Click += delegate { SaveUIToConfig(); ActionRunner.Execute(_config); };
            tblButtons.Controls.Add(_btnTest, 0, 0);

            _btnSave = new Button { Text = "&Save", FlatStyle = FlatStyle.System, AutoSize = true, MinimumSize = new Size(75, 23), TabIndex = 7 };
            _btnSave.Click += BtnSave_Click;
            tblButtons.Controls.Add(_btnSave, 2, 0);

            _btnClose = new Button { Text = "&Close", FlatStyle = FlatStyle.System, AutoSize = true, MinimumSize = new Size(75, 23), Margin = new Padding(6, 0, 0, 0), TabIndex = 8 };
            _btnClose.Click += delegate { this.Hide(); };
            tblButtons.Controls.Add(_btnClose, 3, 0);

            this.AcceptButton = _btnSave;
            this.CancelButton = _btnClose;

            // Status Bar
            var statusStrip = new StatusStrip { RenderMode = ToolStripRenderMode.System, SizingGrip = false, Dock = DockStyle.Bottom };
            _statusLabel = new ToolStripStatusLabel { Spring = true, TextAlign = ContentAlignment.MiddleLeft, Text = "Ready. Press your physical Copilot key to test." };
            statusStrip.Items.Add(_statusLabel);

            // Root Layout
            var rootLayout = new TableLayoutPanel { Dock = DockStyle.Fill, AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, ColumnCount = 1, RowCount = 3, Padding = new Padding(12, 10, 12, 12) };
            rootLayout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
            rootLayout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            rootLayout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            rootLayout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            rootLayout.Controls.Add(grpAction, 0, 0);
            rootLayout.Controls.Add(grpOptions, 0, 1);
            rootLayout.Controls.Add(tblButtons, 0, 2);

            this.Controls.Add(rootLayout);
            this.Controls.Add(statusStrip);
            this.FormClosing += delegate(object s, FormClosingEventArgs e) { if (e.CloseReason == CloseReason.UserClosing) { e.Cancel = true; this.Hide(); } };
        }

        private void UpdateTargetPrompt()
        {
            this.SuspendLayout();
            if (_comboAction.SelectedIndex == 1) // URL
            {
                _lblTarget.Text = "&Website URL:";
                if (!_textTarget.Text.StartsWith("http", StringComparison.OrdinalIgnoreCase)) _textTarget.Text = "https://claude.ai";
                _btnBrowse.Visible = false;
                _lblArgs.Visible = false;
                _textArgs.Visible = false;
                _checkAdmin.Visible = false;
            }
            else if (_comboAction.SelectedIndex == 2) // PowerShell
            {
                _lblTarget.Text = "&PowerShell command or script:";
                if (_textTarget.Text.StartsWith("http", StringComparison.OrdinalIgnoreCase)) _textTarget.Text = "";
                _btnBrowse.Visible = false;
                _lblArgs.Visible = false;
                _textArgs.Visible = false;
                _checkAdmin.Visible = true;
            }
            else // Launch App
            {
                _lblTarget.Text = "&Application or file to open:";
                if (_textTarget.Text.StartsWith("http", StringComparison.OrdinalIgnoreCase)) _textTarget.Text = "wt.exe";
                _btnBrowse.Visible = true;
                _lblArgs.Visible = true;
                _textArgs.Visible = true;
                _checkAdmin.Visible = true;
            }
            this.ResumeLayout(true);
        }

        private void BtnBrowse_Click(object sender, EventArgs e)
        {
            using (var ofd = new OpenFileDialog())
            {
                ofd.Title = "Select Application or File";
                ofd.Filter = "Applications & Shortcuts (*.exe;*.bat;*.cmd;*.lnk)|*.exe;*.bat;*.cmd;*.lnk|All Files (*.*)|*.*";
                if (ofd.ShowDialog() == DialogResult.OK) _textTarget.Text = ofd.FileName;
            }
        }

        private void LoadConfigToUI()
        {
            if (_config.ActionType == "OpenUrl" || (!string.IsNullOrEmpty(_config.TargetPath) && _config.TargetPath.StartsWith("http", StringComparison.OrdinalIgnoreCase)))
                _comboAction.SelectedIndex = 1;
            else if (_config.ActionType == "RunCommand")
                _comboAction.SelectedIndex = 2;
            else
                _comboAction.SelectedIndex = 0;

            _textTarget.Text = string.IsNullOrEmpty(_config.TargetPath) ? "wt.exe" : _config.TargetPath;
            _textArgs.Text = _config.Arguments ?? "";
            _checkAdmin.Checked = _config.RunAsAdmin;
            _checkStartup.Checked = ConfigManager.IsStartupEnabled();
            UpdateTargetPrompt();
        }

        private void SaveUIToConfig()
        {
            _config.ActionType = _comboAction.SelectedIndex == 1 ? "OpenUrl" : (_comboAction.SelectedIndex == 2 ? "RunCommand" : "LaunchApp");
            _config.TargetPath = _textTarget.Text.Trim();
            _config.Arguments = _textArgs.Text.Trim();
            _config.RunAsAdmin = _checkAdmin.Checked;
            _config.StartWithWindows = _checkStartup.Checked;
        }

        private void BtnSave_Click(object sender, EventArgs e)
        {
            SaveUIToConfig();
            ConfigManager.Save(_config);
            ConfigManager.SetStartup(_config.StartWithWindows);
            MessageBox.Show("Settings saved.", "Copilot Remapper", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }
    }

    public class TrayAppContext : ApplicationContext
    {
        private readonly NotifyIcon _trayIcon;
        private readonly KeyboardHook _hook;
        private SettingsForm _settingsForm;
        private static Icon _appIcon;

        public TrayAppContext(bool startMinimized)
        {
            _hook = new KeyboardHook();

            var menu = new ContextMenu();
            menu.MenuItems.Add(new MenuItem("Settings", delegate { ShowSettings(); }));
            menu.MenuItems.Add(new MenuItem("Exit", delegate { ExitApp(); }));

            _trayIcon = new NotifyIcon
            {
                Icon = GetAppIcon(),
                ContextMenu = menu,
                Text = "Copilot Key Remapper",
                Visible = true
            };
            _trayIcon.DoubleClick += delegate { ShowSettings(); };

            ThreadPool.QueueUserWorkItem(delegate
            {
                try
                {
                    using (var evt = new EventWaitHandle(false, EventResetMode.AutoReset, "Global\\CopilotRemapper_Wakeup"))
                    {
                        while (evt.WaitOne())
                        {
                            if (_settingsForm != null && !_settingsForm.IsDisposed)
                                _settingsForm.BeginInvoke((Action)delegate { ShowSettings(); });
                            else
                                ShowSettings();
                        }
                    }
                }
                catch { }
            });

            if (!startMinimized) ShowSettings();
        }

        public void ShowSettings()
        {
            if (_settingsForm == null || _settingsForm.IsDisposed) _settingsForm = new SettingsForm(_hook);
            _settingsForm.Show();
            _settingsForm.WindowState = FormWindowState.Normal;
            _settingsForm.BringToFront();
            _settingsForm.Activate();
        }

        private void ExitApp()
        {
            _trayIcon.Visible = false;
            _hook.Dispose();
            Application.Exit();
        }

        public static Icon GetAppIcon()
        {
            if (_appIcon != null) return _appIcon;
            using (var bmp = new Bitmap(32, 32))
            using (var g = Graphics.FromImage(bmp))
            {
                g.SmoothingMode = SmoothingMode.AntiAlias;
                g.Clear(Color.Transparent);
                using (var path = new GraphicsPath())
                {
                    int r = 6;
                    path.AddArc(1, 1, r, r, 180, 90);
                    path.AddArc(25, 1, r, r, 270, 90);
                    path.AddArc(25, 25, r, r, 0, 90);
                    path.AddArc(1, 25, r, r, 90, 90);
                    path.CloseFigure();
                    using (var b = new SolidBrush(Color.FromArgb(0, 103, 192))) g.FillPath(b, path);
                    using (var p = new Pen(Color.FromArgb(180, 215, 255), 1.5f)) g.DrawPath(p, path);
                }
                using (var font = new Font("Segoe UI", 14F, FontStyle.Bold, GraphicsUnit.Pixel))
                using (var tb = new SolidBrush(Color.White))
                {
                    var sf = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center };
                    g.DrawString("C", font, tb, new RectangleF(0, 1, 32, 32), sf);
                }
                IntPtr hIcon = bmp.GetHicon();
                _appIcon = (Icon)Icon.FromHandle(hIcon).Clone();
                NativeTheme.DestroyIcon(hIcon);
                return _appIcon;
            }
        }
    }

    public static class Program
    {
        [DllImport("user32.dll")]
        private static extern bool SetProcessDpiAwarenessContext(IntPtr ctx);
        [DllImport("user32.dll")]
        private static extern bool SetProcessDPIAware();
        [DllImport("kernel32.dll")]
        private static extern bool AttachConsole(int dwProcessId);

        [STAThread]
        public static void Main(string[] args)
        {
            if (AttachConsole(-1))
            {
                try { Console.SetOut(new StreamWriter(Console.OpenStandardOutput()) { AutoFlush = true }); } catch { }
            }
            try { if (!SetProcessDpiAwarenessContext((IntPtr)(-4))) SetProcessDPIAware(); } catch { try { SetProcessDPIAware(); } catch { } }

            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);

            bool startMinimized = false;
            foreach (string arg in args)
            {
                if (arg.Equals("--silent", StringComparison.OrdinalIgnoreCase) || arg.Equals("-s", StringComparison.OrdinalIgnoreCase))
                    startMinimized = true;
                else if (arg.Equals("--test", StringComparison.OrdinalIgnoreCase) || arg.Equals("-t", StringComparison.OrdinalIgnoreCase))
                {
                    var cfg = ConfigManager.Load();
                    Console.WriteLine("[Self-Test] Configuration loaded: " + cfg.ActionType + " -> " + cfg.TargetPath);
                    return;
                }
                else if (arg.Equals("--install-startup", StringComparison.OrdinalIgnoreCase))
                {
                    ConfigManager.SetStartup(true);
                    Console.WriteLine("Startup enabled.");
                    return;
                }
                else if (arg.Equals("--uninstall-startup", StringComparison.OrdinalIgnoreCase))
                {
                    ConfigManager.SetStartup(false);
                    Console.WriteLine("Startup disabled.");
                    return;
                }
            }

            bool createdNew;
            using (var mutex = new Mutex(true, "Global\\CopilotRemapper_SingleInstance_Mutex", out createdNew))
            {
                if (!createdNew)
                {
                    try
                    {
                        using (var evt = EventWaitHandle.OpenExisting("Global\\CopilotRemapper_Wakeup"))
                            evt.Set();
                    }
                    catch { }
                    return;
                }

                Application.Run(new TrayAppContext(startMinimized));
            }
        }
    }
}
