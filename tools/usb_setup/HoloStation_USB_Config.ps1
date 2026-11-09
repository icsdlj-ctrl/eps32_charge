$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()

$script:port = $null
$script:buffer = ''
$script:lastQuery = [DateTime]::MinValue
$script:deviceIP = ''
$script:savedPassword = ''
$script:firmwareKeepsPassword = $false
$script:logFail = ''
$script:fillingWifi = $false
$script:alertSent = ''
$script:alertSentAt = [DateTime]::MinValue
$script:canvas = [System.Drawing.Color]::FromArgb(245, 245, 247)
$script:sidebar = [System.Drawing.Color]::FromArgb(232, 232, 237)
$script:ink = [System.Drawing.Color]::FromArgb(29, 29, 31)
$script:muted = [System.Drawing.Color]::FromArgb(110, 110, 115)
$script:line = [System.Drawing.Color]::FromArgb(229, 229, 234)
$script:blue = [System.Drawing.Color]::FromArgb(0, 113, 227)
$script:blueHot = [System.Drawing.Color]::FromArgb(10, 132, 255)
$script:green = [System.Drawing.Color]::FromArgb(36, 138, 61)
$script:red = [System.Drawing.Color]::FromArgb(215, 0, 21)
$script:orange = [System.Drawing.Color]::FromArgb(199, 106, 0)
$script:well = [System.Drawing.Color]::FromArgb(245, 245, 247)
$script:fontTitle = New-Object System.Drawing.Font('Segoe UI', 22, [System.Drawing.FontStyle]::Regular)
$script:fontHero = New-Object System.Drawing.Font('Segoe UI', 20, [System.Drawing.FontStyle]::Regular)
$script:fontNav = New-Object System.Drawing.Font('Microsoft YaHei UI', 11)
$script:fontNavOn = New-Object System.Drawing.Font('Microsoft YaHei UI', 11, [System.Drawing.FontStyle]::Bold)
$script:fontBody = New-Object System.Drawing.Font('Microsoft YaHei UI', 10)
$script:fontSmall = New-Object System.Drawing.Font('Microsoft YaHei UI', 9)
$script:fontSection = New-Object System.Drawing.Font('Microsoft YaHei UI', 18)
$script:brushInk = New-Object System.Drawing.SolidBrush($script:ink)
$script:brushCard = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::White)
$script:brushPick = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(232, 232, 237))

function Round-Region($control, [int]$radius) {
    $w = [Math]::Max(1, $control.Width - 1)
    $h = [Math]::Max(1, $control.Height - 1)
    $d = [Math]::Min($radius * 2, [Math]::Min($w, $h))
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddArc(0, 0, $d, $d, 180, 90)
    $path.AddArc($w - $d, 0, $d, $d, 270, 90)
    $path.AddArc($w - $d, $h - $d, $d, $d, 0, 90)
    $path.AddArc(0, $h - $d, $d, $d, 90, 90)
    $path.CloseFigure()
    $control.Region = New-Object System.Drawing.Region($path)
}
function Add-Label($parent, $text, $x, $y, $w, $h, $color, $font) {
    $c = New-Object System.Windows.Forms.Label
    $c.Text = $text
    $c.SetBounds($x, $y, $w, $h)
    $c.ForeColor = $color
    $c.Font = $font
    $c.BackColor = $parent.BackColor
    $parent.Controls.Add($c)
    return $c
}
function Add-Card($parent, $x, $y, $w, $h) {
    $c = New-Object System.Windows.Forms.Panel
    $c.SetBounds($x, $y, $w, $h)
    $c.BackColor = [System.Drawing.Color]::White
    $parent.Controls.Add($c)
    Round-Region $c 18
    return $c
}
function Add-Button($parent, $text, $x, $y, $w, $primary) {
    $c = New-Object System.Windows.Forms.Button
    $c.Text = $text
    $c.SetBounds($x, $y, $w, 36)
    $c.FlatStyle = 'Flat'
    $c.FlatAppearance.BorderSize = 0
    $c.Cursor = [System.Windows.Forms.Cursors]::Hand
    $c.Font = $script:fontBody
    if ($primary) {
        $c.BackColor = $script:blue
        $c.ForeColor = [System.Drawing.Color]::White
        $c.FlatAppearance.MouseOverBackColor = $script:blueHot
        $c.FlatAppearance.MouseDownBackColor = [System.Drawing.Color]::FromArgb(0, 84, 166)
    } else {
        $c.BackColor = $script:sidebar
        $c.ForeColor = $script:ink
        $c.FlatAppearance.MouseOverBackColor = [System.Drawing.Color]::FromArgb(220, 220, 226)
        $c.FlatAppearance.MouseDownBackColor = [System.Drawing.Color]::FromArgb(210, 210, 216)
    }
    $parent.Controls.Add($c)
    Round-Region $c 12
    return $c
}
function Add-Field($parent, $x, $y, $w) {
    $well = New-Object System.Windows.Forms.Panel
    $well.SetBounds($x, $y, $w, 40)
    $well.BackColor = $script:well
    $parent.Controls.Add($well)
    Round-Region $well 12
    $box = New-Object System.Windows.Forms.TextBox
    $box.BorderStyle = 'None'
    $box.SetBounds(14, 10, $w - 28, 22)
    $box.BackColor = $script:well
    $box.ForeColor = $script:ink
    $box.Font = $script:fontBody
    $well.Controls.Add($box)
    return $box
}
function Add-List($parent, $x, $y, $w, $h) {
    $box = New-Object System.Windows.Forms.ListBox
    $box.SetBounds($x, $y, $w, $h)
    $box.BorderStyle = 'None'
    $box.BackColor = [System.Drawing.Color]::White
    $box.ForeColor = $script:ink
    $box.Font = $script:fontBody
    $box.IntegralHeight = $false
    $box.DrawMode = 'OwnerDrawFixed'
    $box.ItemHeight = 34
    $box.Add_DrawItem({
        param($sender, $e)
        if ($e.Index -lt 0) { return }
        $selected = ($e.State -band [System.Windows.Forms.DrawItemState]::Selected) -ne 0
        $e.Graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::ClearTypeGridFit
        $e.Graphics.FillRectangle($(if ($selected) { $script:brushPick } else { $script:brushCard }), $e.Bounds)
        $item = $sender.Items[$e.Index]
        $text = if ($item.Label) { [string]$item.Label } else { [string]$item }
        $e.Graphics.DrawString($text, $script:fontBody, $script:brushInk, ($e.Bounds.X + 14), ($e.Bounds.Y + 7))
    })
    $parent.Controls.Add($box)
    return $box
}

$form = New-Object System.Windows.Forms.Form
$form.Text = 'HoloStation'
$form.ClientSize = New-Object System.Drawing.Size(980, 720)
$form.StartPosition = 'CenterScreen'
$form.FormBorderStyle = 'FixedSingle'
$form.MaximizeBox = $false
$form.BackColor = $script:canvas
$form.ForeColor = $script:ink
$form.Font = $script:fontBody
$form.MinimumSize = $form.Size
$prop = $form.GetType().GetProperty('DoubleBuffered', [System.Reflection.BindingFlags]'Instance,NonPublic')
if ($prop) { $prop.SetValue($form, $true, $null) }

$side = New-Object System.Windows.Forms.Panel
$side.SetBounds(0, 0, 216, 720)
$side.BackColor = $script:sidebar
$form.Controls.Add($side)
$null = Add-Label $side 'HoloStation' 24 28 170 36 $script:ink $script:fontTitle
$null = Add-Label $side '桌面充电与天气' 26 68 170 24 $script:muted $script:fontSmall

$content = New-Object System.Windows.Forms.Panel
$content.SetBounds(216, 0, 764, 640)
$content.BackColor = $script:canvas
$form.Controls.Add($content)
$footer = New-Object System.Windows.Forms.Panel
$footer.SetBounds(216, 640, 764, 80)
$footer.BackColor = $script:canvas
$form.Controls.Add($footer)
$message = Add-Label $footer '密码只保存在设备里。Cursor 和 Codex 的确认、完成状态会从这里送到屏幕。' 28 16 700 48 $script:muted $script:fontSmall

function New-Page {
    $page = New-Object System.Windows.Forms.Panel
    $page.SetBounds(0, 0, 764, 640)
    $page.BackColor = $script:canvas
    $page.Visible = $false
    $content.Controls.Add($page)
    return $page
}
$overview = New-Page
$wifiPage = New-Page
$blePage = New-Page
$weatherPage = New-Page
$screenPage = New-Page
$script:pages = @{ overview = $overview; wifi = $wifiPage; ble = $blePage; weather = $weatherPage; screen = $screenPage }
$script:nav = @()
function Add-Nav($text, $key, $y) {
    $b = New-Object System.Windows.Forms.Button
    $b.Text = '   ' + $text
    $b.Tag = $key
    $b.SetBounds(16, $y, 184, 40)
    $b.FlatStyle = 'Flat'
    $b.FlatAppearance.BorderSize = 0
    $b.TextAlign = 'MiddleLeft'
    $b.Cursor = [System.Windows.Forms.Cursors]::Hand
    $b.Font = $script:fontNav
    $b.ForeColor = $script:ink
    $b.BackColor = $script:sidebar
    $side.Controls.Add($b)
    Round-Region $b 12
    $b.Add_Click({ param($sender, $e) Show-Page $sender.Tag })
    $script:nav += $b
}
function Show-Page($key) {
    foreach ($name in $script:pages.Keys) { $script:pages[$name].Visible = ($name -eq $key) }
    foreach ($b in $script:nav) {
        $on = [string]$b.Tag -eq [string]$key
        $b.BackColor = $(if ($on) { [System.Drawing.Color]::White } else { $script:sidebar })
        $b.Font = $(if ($on) { $script:fontNavOn } else { $script:fontNav })
        $b.FlatAppearance.MouseOverBackColor = $(if ($on) { [System.Drawing.Color]::White } else { [System.Drawing.Color]::FromArgb(222, 222, 228) })
    }
}
Add-Nav '概览' 'overview' 118
Add-Nav '无线局域网' 'wifi' 166
Add-Nav '蓝牙' 'ble' 214
Add-Nav '天气' 'weather' 262
Add-Nav '屏幕' 'screen' 310

$hero = Add-Card $overview 28 24 704 124
$heroState = Add-Label $hero '未连接' 28 26 420 40 $script:ink $script:fontHero
$heroDetail = Add-Label $hero '用 USB 接上设备后，会自动连接并保持通信。' 28 72 640 32 $script:muted $script:fontSmall
$script:portWatt = @{}
$script:portSub = @{}
$px = 28
foreach ($name in @('A1', 'C1', 'C2', 'C3')) {
    $card = Add-Card $overview $px 164 166 118
    $null = Add-Label $card $name 18 14 130 22 $script:muted $script:fontSmall
    $script:portWatt[$name] = Add-Label $card '--' 16 40 134 36 $script:ink $script:fontHero
    $script:portSub[$name] = Add-Label $card '未检测' 18 82 134 22 $script:muted $script:fontSmall
    $px += 179
}
$alertCard = Add-Card $overview 28 298 704 132
$alertTitle = Add-Label $alertCard '空闲' 28 22 360 36 $script:ink $script:fontHero
$alertBody = Add-Label $alertCard 'Cursor 或 Codex 需要你确认、或改完代码时，这里和设备屏幕会一起提示。' 28 66 470 48 $script:muted $script:fontSmall
$clearAlert = Add-Button $alertCard '关闭提示' 528 46 148 $false

$null = Add-Label $wifiPage '无线局域网' 28 20 400 36 $script:ink $script:fontSection
$null = Add-Label $wifiPage '附近网络' 28 64 300 22 $script:muted $script:fontSmall
$null = Add-Label $wifiPage '已记住，最多 5 个' 392 64 300 22 $script:muted $script:fontSmall
$nearCard = Add-Card $wifiPage 28 90 332 210
$savedCard = Add-Card $wifiPage 376 90 332 210
$nearby = Add-List $nearCard 8 8 316 194
$savedWifi = Add-List $savedCard 8 8 316 194
$scanWifi = Add-Button $wifiPage '扫描附近网络' 28 316 180 $false
$ssid = Add-Field $wifiPage 28 372 420
$password = Add-Field $wifiPage 28 428 420
$password.UseSystemPasswordChar = $true
$null = Add-Label $wifiPage '名称' 460 372 80 22 $script:muted $script:fontSmall
$null = Add-Label $wifiPage '密码' 460 428 80 22 $script:muted $script:fontSmall
$show = New-Object System.Windows.Forms.CheckBox
$show.Text = '显示密码'
$show.SetBounds(460, 456, 120, 28)
$show.ForeColor = $script:muted
$show.BackColor = $script:canvas
$show.Font = $script:fontSmall
$wifiPage.Controls.Add($show)
$show.Add_CheckedChanged({ $password.UseSystemPasswordChar = -not $show.Checked })
$save = Add-Button $wifiPage '保存并连接' 28 492 180 $true
$web = Add-Button $wifiPage '打开设备网页' 220 492 180 $false
$null = Add-Label $wifiPage '点选列表只填入名称。密码留空时，沿用这个名称已经记住的密码。开机后自动连接信号最好的一个。' 28 544 680 48 $script:muted $script:fontSmall

$null = Add-Label $blePage '蓝牙' 28 20 400 36 $script:ink $script:fontSection
$null = Add-Label $blePage '附近设备，显示百分比和信号' 28 62 400 22 $script:muted $script:fontSmall
$bleCard = Add-Card $blePage 28 96 704 360
$bleList = Add-List $bleCard 12 12 680 336
$scanBle = Add-Button $blePage '扫描附近蓝牙' 28 476 180 $true

$null = Add-Label $weatherPage '天气' 28 20 400 36 $script:ink $script:fontSection
$weatherCard = Add-Card $weatherPage 28 72 704 300
$locMode = New-Object System.Windows.Forms.ComboBox
$locMode.SetBounds(28, 28, 240, 32)
$locMode.DropDownStyle = 'DropDownList'
$locMode.FlatStyle = 'Flat'
$locMode.Font = $script:fontBody
$locMode.BackColor = $script:well
$null = $locMode.Items.Add('自适应（公网 IP）')
$null = $locMode.Items.Add('自定义位置')
$locMode.SelectedIndex = 0
$weatherCard.Controls.Add($locMode)
$null = Add-Label $weatherCard '城市' 292 4 80 20 $script:muted $script:fontSmall
$locCity = Add-Field $weatherCard 292 28 380
$null = Add-Label $weatherCard '纬度' 28 88 80 20 $script:muted $script:fontSmall
$null = Add-Label $weatherCard '经度' 250 88 80 20 $script:muted $script:fontSmall
$null = Add-Label $weatherCard '时区' 472 88 120 20 $script:muted $script:fontSmall
$locLat = Add-Field $weatherCard 28 112 200
$locLon = Add-Field $weatherCard 250 112 200
$locZone = Add-Field $weatherCard 472 112 200
$saveLocation = Add-Button $weatherCard '保存位置' 28 176 160 $true
$refresh = Add-Button $weatherCard '刷新天气' 204 176 160 $false
$null = Add-Label $weatherCard '只填城市时，设备会查询经纬度和时区。自定义位置在升级后仍会保留。' 28 230 640 48 $script:muted $script:fontSmall
$weatherStatus = Add-Label $weatherPage '等待设备连接。' 32 392 680 80 $script:ink $script:fontBody

$null = Add-Label $screenPage '屏幕' 28 20 400 36 $script:ink $script:fontSection
$null = Add-Label $screenPage '选择设备现在显示的页面。' 28 64 400 24 $script:muted $script:fontSmall
$auto = Add-Button $screenPage '自动轮播' 28 112 160 $false
$clock = Add-Button $screenPage '时钟' 204 112 160 $false
$weather = Add-Button $screenPage '天气' 380 112 160 $false
$charge = Add-Button $screenPage '充电' 556 112 140 $false
$screenCard = Add-Card $screenPage 28 180 704 180
$null = Add-Label $screenCard '维护' 28 24 200 28 $script:ink $script:fontHero
$null = Add-Label $screenCard '停止应用会在重启间隙抓住设备，方便串口保持连接。恢复运行会重新启动 Wi-Fi 和天气。' 28 64 640 48 $script:muted $script:fontSmall
$stopApp = Add-Button $screenCard '停止应用' 28 124 160 $false
$runApp = Add-Button $screenCard '恢复运行' 204 124 160 $true

$connCard = Add-Card $side 16 620 184 80
$ports = New-Object System.Windows.Forms.ComboBox
$ports.SetBounds(12, 10, 160, 28)
$ports.DropDownStyle = 'DropDownList'
$ports.FlatStyle = 'Flat'
$ports.Font = $script:fontSmall
$ports.BackColor = [System.Drawing.Color]::White
$connCard.Controls.Add($ports)
$connect = Add-Button $connCard '连接' 12 40 76 $true
$scan = Add-Button $connCard '刷新' 96 40 76 $false

function Explain-DeviceError($name) {
    switch ($name) {
        'ESP_ERR_NOT_FOUND' { return '这个 Wi-Fi 还没有保存过密码。请填写至少 8 位；开放网络可以留空。' }
        'ESP_ERR_INVALID_ARG' { return '参数无效。Wi-Fi 名称 1–32 字；新密码至少 8 位。自定义位置需要城市。' }
        'ESP_ERR_INVALID_STATE' { return '设备正忙，或天气服务还没启动。' }
        'ESP_ERR_TIMEOUT' { return '扫描超时，请再试一次。' }
        'ESP_ERR_NOT_SUPPORTED' { return '当前固件还不认识这条命令，需要烧录这次更新。' }
        default { return [string]$name }
    }
}
function Format-WifiReason([int]$reason) {
    switch ($reason) {
        15 { return '密码错误或握手超时' }
        201 { return '找不到这个 Wi-Fi，请确认名称和 2.4 GHz' }
        202 { return '认证失败，请核对密码' }
        203 { return '已找到热点，但关联失败' }
        204 { return '握手超时，多半是密码错误' }
        200 { return '信号中断，路由器无响应' }
        2 { return '认证超时' }
        8 { return '路由器主动断开了连接' }
        23 { return '认证失败' }
        0 { return '' }
        default { return "联网失败，原因码 $reason" }
    }
}
function Set-Note($text, $kind) {
    $message.Text = $text
    $message.ForeColor = switch ($kind) {
        'bad' { $script:red }
        'good' { $script:green }
        default { $script:muted }
    }
}
function Fill-Ports {
    $keep = [string]$ports.SelectedItem
    $ports.Items.Clear()
    foreach ($p in [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object) { $null = $ports.Items.Add($p) }
    if ($keep -and $ports.Items.Contains($keep)) { $ports.SelectedItem = $keep }
    elseif ($ports.Items.Contains('COM7')) { $ports.SelectedItem = 'COM7' }
    elseif ($ports.Items.Contains('COM6')) { $ports.SelectedItem = 'COM6' }
    elseif ($ports.Items.Count) { $ports.SelectedIndex = 0 }
}
function Send-Command($command) {
    if (-not $script:port -or -not $script:port.IsOpen) { throw '请先连接设备。' }
    $json = ConvertTo-Json -InputObject $command -Depth 10 -Compress
    $script:port.WriteLine($json)
    $script:lastQuery = [DateTime]::Now
}
function Do-Action($action) {
    try { & $action }
    catch { Set-Note $_.Exception.Message 'bad' }
}
function Fill-WifiList($box, $items, $map) {
    if ($box.Focused) { return }
    $script:fillingWifi = $true
    $keep = if ($box.SelectedItem) { [string]$box.SelectedItem.Ssid } else { '' }
    $box.Items.Clear()
    foreach ($item in @($items)) {
        if ($null -eq $item -or $item -eq '') { continue }
        [void]$box.Items.Add((& $map $item))
    }
    $box.DisplayMember = 'Label'
    for ($i = 0; $i -lt $box.Items.Count; $i++) {
        if ($box.Items[$i].Ssid -eq $keep) { $box.SelectedIndex = $i; break }
    }
    $script:fillingWifi = $false
}
function Use-SelectedWifi($box) {
    if ($script:fillingWifi -or -not $box.SelectedItem) { return }
    $ssid.Text = [string]$box.SelectedItem.Ssid
}
function Apply-Status($d) {
    try {
        $copy = $d | ConvertTo-Json -Depth 10
        [System.IO.File]::WriteAllText((Join-Path $PSScriptRoot 'HoloStation_last_status.json'), $copy, (New-Object System.Text.UTF8Encoding($false)))
    } catch {}
    if ($d.PSObject.Properties.Name -contains 'wifiReason') { $script:firmwareKeepsPassword = $true }
    if ($d.error) {
        Set-Note ('设备返回：' + (Explain-DeviceError ([string]$d.error))) 'bad'
        return
    }
    $script:deviceIP = [string]$d.ip
    $fail = ''
    if ($d.wifiFail) { $fail = [string]$d.wifiFail }
    elseif ($d.wifiReason -and [int]$d.config.wifiState -ne 2) { $fail = Format-WifiReason ([int]$d.wifiReason) }
    elseif ($script:logFail -and [int]$d.config.wifiState -ne 2) { $fail = $script:logFail }
    $ssidText = if ($d.ssid) { [string]$d.ssid } else { '未连接网络' }
    if ($d.appHeld) {
        $heroState.Text = '应用已停止'
        $heroDetail.Text = "$($d.config.Ver)  ·  Wi-Fi 不会启动，需要时在屏幕页恢复运行。"
    } elseif ([int]$d.config.wifiState -eq 2) {
        $signal = if ($null -ne $d.wifiSignal) { "  ·  $($d.wifiSignal)%  $($d.wifiRssi) dBm" } else { '' }
        $heroState.Text = '已连接'
        $heroDetail.Text = "$ssidText  ·  $($d.ip)$signal  ·  $($d.config.Ver)"
        $heroState.ForeColor = $script:ink
    } else {
        $why = if ($fail) { $fail } else { '还没有拿到 IP' }
        $heroState.Text = '未联网'
        $heroDetail.Text = "$ssidText  ·  $why"
    }
    if ($d.apSsid) { $heroDetail.Text += "`r`n热点 $($d.apSsid)  ·  http://192.168.4.1/" }
    foreach ($name in @('A1', 'C1', 'C2', 'C3')) {
        $p = $d.$name
        if ($p -and $p.valid) {
            $watts = '{0:0.0} W' -f [double]$p.P
            $script:portWatt[$name].Text = $watts
            $script:portSub[$name].Text = ('{0:0.0} V  ·  {1:0.00} A' -f [double]$p.V, [double]$p.I)
        } else {
            $script:portWatt[$name].Text = '--'
            $script:portSub[$name].Text = '未检测'
        }
    }
    $mode = if ($d.weather.mode) { [string]$d.weather.mode } elseif ($d.weather.autoLocation) { 'auto' } else { '' }
    if (-not $locMode.Focused) {
        if ($mode -eq 'auto') { $locMode.SelectedIndex = 0 } elseif ($mode -eq 'custom') { $locMode.SelectedIndex = 1 }
    }
    if (-not $locCity.Focused -and $d.weather.city -and $d.weather.city -ne 'IP定位') { $locCity.Text = [string]$d.weather.city }
    if (-not $locZone.Focused -and $d.weather.timezone) { $locZone.Text = [string]$d.weather.timezone }
    if (-not $locLat.Focused -and $null -ne $d.weather.latitude) { $locLat.Text = [string]$d.weather.latitude }
    if (-not $locLon.Focused -and $null -ne $d.weather.longitude) { $locLon.Text = [string]$d.weather.longitude }
    $place = if ($mode -eq 'custom') { '自定义' } else { '自适应' }
    if ($d.weather.valid) {
        $suffix = if ($d.weather.stale) { '  ·  缓存' } else { '' }
        $weatherStatus.Text = "$place  ·  $($d.weather.city)`r`n$($d.weather.temperature) °C  ·  $($d.weather.condition)  ·  湿度 $($d.weather.humidity)%$suffix"
    } else {
        $err = if ($d.weather.error) { '  ·  ' + (Explain-DeviceError ([string]$d.weather.error)) } else { '' }
        $weatherStatus.Text = "本地天气（$place）还在定位和校时，首次大约一分钟。$err"
    }
    if ($fail -and [int]$d.config.wifiState -ne 2) { Set-Note $fail 'bad' }
    $scanMap = @{}
    if ($d.wifiScan) {
        foreach ($item in @($d.wifiScan)) { if ($item.ssid) { $scanMap[[string]$item.ssid] = $item } }
    }
    $savedRows = @()
    if ($d.wifiSaved) {
        foreach ($name in @($d.wifiSaved)) {
            if (-not $name) { continue }
            $hit = $scanMap[[string]$name]
            $label = [string]$name
            if ($hit) { $label = '{0}    {1}%    {2} dBm' -f $name, $hit.signal, $hit.rssi }
            $savedRows += [pscustomobject]@{ Ssid = [string]$name; Label = $label }
        }
    }
    Fill-WifiList $savedWifi $savedRows { param($item) $item }
    Fill-WifiList $nearby $(if ($d.wifiScan) { @($d.wifiScan) } else { @() }) { param($item) $mark = if ($item.open) { '    开放' } else { '' }; [pscustomobject]@{ Ssid = [string]$item.ssid; Label = ('{0}    {1}%    {2} dBm{3}' -f $item.ssid, $item.signal, $item.rssi, $mark) } }
    Fill-WifiList $bleList $(if ($d.bleScan) { @($d.bleScan) } else { @() }) { param($item) [pscustomobject]@{ Ssid = [string]$item.addr; Label = ('{0}    {1}%    {2} dBm' -f $(if ($item.name) { $item.name } else { $item.addr }), $item.signal, $item.rssi) } }
}
function Note-LogLine($line) {
    if ($line -match 'Wi-Fi disconnected reason\s+(\d+)') {
        $script:logFail = Format-WifiReason ([int]$Matches[1])
        Set-Note $script:logFail 'bad'
    } elseif ($line -match 'station IP\s+(\S+)') {
        $script:logFail = ''
        $script:deviceIP = $Matches[1]
    }
}
function Close-DevicePort {
    if ($script:port) {
        if ($script:port.IsOpen) { $script:port.Close() }
        $script:port.Dispose()
        $script:port = $null
    }
    $connect.Text = '连接'
}
function Send-BootSignal([string]$word) {
    if (-not $ports.SelectedItem) { throw '请先选择串口。' }
    Close-DevicePort
    $name = [string]$ports.SelectedItem
    $deadline = (Get-Date).AddSeconds(8)
    $saw = $false
    while ((Get-Date) -lt $deadline -and -not $saw) {
        $probe = $null
        try {
            $probe = New-Object System.IO.Ports.SerialPort($name, 115200, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
            $probe.Encoding = New-Object System.Text.UTF8Encoding($false)
            $probe.NewLine = "`n"
            $probe.ReadTimeout = 50
            $probe.WriteTimeout = 200
            $probe.DtrEnable = $false
            $probe.RtsEnable = $false
            $probe.Open()
            $probe.DtrEnable = $false
            $probe.RtsEnable = $true
            Start-Sleep -Milliseconds 100
            $probe.RtsEnable = $false
            $text = ''
            $until = (Get-Date).AddMilliseconds(2200)
            while ((Get-Date) -lt $until) {
                try { $probe.WriteLine($word) } catch { break }
                Start-Sleep -Milliseconds 30
                try { $text += $probe.ReadExisting() } catch {}
                if ($text.Contains('APP_HELD') -or $text.Contains('APP_RUN')) { $saw = $true; break }
            }
        } catch {
            Start-Sleep -Milliseconds 120
        } finally {
            if ($probe) {
                if ($probe.IsOpen) { $probe.Close() }
                $probe.Dispose()
            }
        }
    }
    if (-not $saw) { throw '没有在重启间隙抓住设备。请确认 USB 还插着，再按一次。' }
}
function Write-AlertFile($mode, $source) {
    $dir = Join-Path $env:LOCALAPPDATA 'HoloStation'
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $payload = @{ mode = $mode; source = $source; at = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() }
    $utf8 = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllText((Join-Path $dir 'alert.json'), ($payload | ConvertTo-Json -Compress), $utf8)
}
function Sync-AgentAlert {
    $path = Join-Path $env:LOCALAPPDATA 'HoloStation\alert.json'
    if (-not (Test-Path -LiteralPath $path)) { return }
    $text = [System.IO.File]::ReadAllText($path)
    $a = $text | ConvertFrom-Json
    $mode = if ($a.mode) { [string]$a.mode } else { 'off' }
    $source = if ($a.source) { [string]$a.source } else { 'cursor' }
    $who = if ($source -eq 'codex') { 'Codex' } else { 'Cursor' }
    if ($mode -eq 'confirm') {
        $alertTitle.Text = '需要确认'
        $alertTitle.ForeColor = $script:orange
        $alertBody.Text = "$who 正在等待你确认。设备屏幕会一直闪，直到这次操作结束。"
    } elseif ($mode -eq 'done') {
        $alertTitle.Text = '已经完成'
        $alertTitle.ForeColor = $script:green
        $alertBody.Text = "$who 这一轮改完了。设备屏幕会一直显示，直到你发出下一条消息。"
    } else {
        $alertTitle.Text = '空闲'
        $alertTitle.ForeColor = $script:ink
        $alertBody.Text = 'Cursor 或 Codex 需要你确认、或改完代码时，这里和设备屏幕会一起提示。'
    }
    if (-not $script:port -or -not $script:port.IsOpen) { return }
    $due = $mode -eq 'confirm' -or $mode -eq 'done'
    $changed = $text -ne $script:alertSent
    $stale = $due -and ((Get-Date) - $script:alertSentAt).TotalSeconds -ge 3
    if (-not $changed -and -not $stale) { return }
    Send-Command @{ setAlert = @{ mode = $mode; source = $source } }
    $script:alertSent = $text
    $script:alertSentAt = Get-Date
}

Fill-Ports
$nearby.Add_SelectedIndexChanged({ Use-SelectedWifi $nearby })
$savedWifi.Add_SelectedIndexChanged({ Use-SelectedWifi $savedWifi })
$scan.Add_Click({ Fill-Ports })
$connect.Add_Click({ Do-Action {
    if ($script:port -and $script:port.IsOpen) {
        Close-DevicePort
        $heroState.Text = '未连接'
        $heroDetail.Text = '用 USB 接上设备后，会自动连接并保持通信。'
        Set-Note '已断开 USB。' 'info'
        return
    }
    if (-not $ports.SelectedItem) { throw '没有检测到串口，请检查 USB 数据线。' }
    $script:port = New-Object System.IO.Ports.SerialPort([string]$ports.SelectedItem, 115200, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
    $script:port.Encoding = New-Object System.Text.UTF8Encoding($false)
    $script:port.NewLine = "`n"
    $script:port.DtrEnable = $false
    $script:port.RtsEnable = $false
    $script:port.ReadTimeout = 500
    $script:port.WriteTimeout = 1500
    $script:port.Open()
    $script:buffer = ''
    $script:logFail = ''
    $script:alertSent = ''
    $connect.Text = '断开'
    $heroState.Text = '正在读取'
    $heroDetail.Text = 'USB 已连接。'
    Send-Command @{ getStatus = @{} }
} })
$stopApp.Add_Click({ Do-Action {
    $form.Cursor = [System.Windows.Forms.Cursors]::WaitCursor
    try {
        Send-BootSignal 'STOP'
        Set-Note '已在重启间隙停住应用。Wi-Fi 不会再启动。' 'good'
        $heroState.Text = '应用已停止'
        $heroDetail.Text = '正在重新连接串口。'
    } finally { $form.Cursor = [System.Windows.Forms.Cursors]::Default }
    $connect.PerformClick()
} })
$runApp.Add_Click({ Do-Action {
    if (-not $script:port -or -not $script:port.IsOpen) { throw '请先连接已经停住的设备，再恢复运行。' }
    Send-Command @{ runApp = @{} }
    Set-Note '已发送恢复运行，设备正在重启。' 'good'
} })
$save.Add_Click({ Do-Action {
    if (-not $ssid.Text) { throw '请填写 Wi-Fi 名称。' }
    $typed = $password.Text
    $send = $typed
    $note = '已保存。设备最多记住 5 个 Wi-Fi，并自动连接信号最好的一个。'
    if (-not $typed) {
        if ($script:firmwareKeepsPassword) {
            $send = ''
            $note = '密码留空，设备将使用这个名称已经记住的密码。'
        } elseif ($script:savedPassword) {
            $send = $script:savedPassword
            $note = '密码留空，已沿用本窗口刚才保存的密码。'
        } else {
            throw '请填写 Wi-Fi 密码。设备确认支持留空沿用原密码之后，再次保存才可以不填。'
        }
    } else {
        if ($typed.Length -lt 8) { throw '新密码至少 8 位。留空才会沿用原来的密码。' }
        $script:savedPassword = $typed
    }
    Send-Command @{ setWiFi = @{ ssid = $ssid.Text; psw = $send } }
    $password.Clear()
    Set-Note $note 'good'
} })
$saveLocation.Add_Click({ Do-Action {
    if ($locMode.SelectedIndex -eq 0) {
        Send-Command @{ setLocation = @{ mode = 'auto' } }
        Set-Note '已改为按公网 IP 自适应定位。' 'good'
    } else {
        if (-not $locCity.Text.Trim()) { throw '自定义位置请填写城市。经纬度可留空，由设备查询。' }
        $body = @{ mode = 'custom'; city = $locCity.Text.Trim() }
        if ($locLat.Text.Trim() -or $locLon.Text.Trim() -or $locZone.Text.Trim()) {
            $lat = 0.0
            $lon = 0.0
            if (-not [double]::TryParse($locLat.Text, [ref]$lat) -or -not [double]::TryParse($locLon.Text, [ref]$lon)) {
                throw '纬度和经度必须是数字，或全部留空让设备按城市查询。'
            }
            if (-not $locZone.Text.Trim()) { throw '填写了经纬度时，请同时填写时区，例如 Asia/Shanghai。' }
            $body.latitude = $lat
            $body.longitude = $lon
            $body.timezone = $locZone.Text.Trim()
        }
        Send-Command @{ setLocation = $body }
        Set-Note '已发送自定义位置。' 'good'
    }
} })
$scanWifi.Add_Click({ Do-Action {
    Send-Command @{ scanWiFi = @{} }
    Set-Note '正在扫描附近的 2.4 GHz 网络。' 'good'
} })
$scanBle.Add_Click({ Do-Action {
    Send-Command @{ scanBle = @{} }
    Set-Note '正在扫描附近蓝牙，大约 3 秒。' 'good'
} })
$auto.Add_Click({ Do-Action { Send-Command @{ setPage = @{ value = 0 } } } })
$charge.Add_Click({ Do-Action { Send-Command @{ setPage = @{ value = 1 } } } })
$clock.Add_Click({ Do-Action { Send-Command @{ setPage = @{ value = 2 } } } })
$weather.Add_Click({ Do-Action { Send-Command @{ setPage = @{ value = 3 } } } })
$refresh.Add_Click({ Do-Action { Send-Command @{ refreshWeather = @{} } } })
$web.Add_Click({ Do-Action {
    $url = if ($script:deviceIP) { 'http://' + $script:deviceIP + '/' } else { 'http://192.168.4.1/' }
    Start-Process $url
} })
$clearAlert.Add_Click({
    Write-AlertFile 'off' 'cursor'
    $script:alertSent = ''
    if ($script:port -and $script:port.IsOpen) {
        Do-Action { Send-Command @{ setAlert = @{ mode = 'off'; source = 'cursor' } } }
    }
    Sync-AgentAlert
})

$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 200
$timer.Add_Tick({
    try { Sync-AgentAlert } catch {}
    if (-not $script:port -or -not $script:port.IsOpen) { return }
    try {
        $script:buffer += $script:port.ReadExisting()
        if ($script:buffer.Length -gt 65536) { $script:buffer = $script:buffer.Substring($script:buffer.Length - 32768) }
        while ($script:buffer.Contains("`n")) {
            $at = $script:buffer.IndexOf("`n")
            $line = $script:buffer.Substring(0, $at)
            $script:buffer = $script:buffer.Substring($at + 1)
            Note-LogLine $line
            $start = $line.IndexOf('HS_REPLY:')
            if ($start -ge 0) {
                try { Apply-Status (ConvertFrom-Json $line.Substring($start + 9)) }
                catch { Set-Note ('状态解析失败：' + $_.Exception.Message) 'bad' }
            }
        }
        if (([DateTime]::Now - $script:lastQuery).TotalSeconds -ge 5) { Send-Command @{ getStatus = @{} } }
    } catch {
        $heroState.Text = '通信中断'
        $heroDetail.Text = $_.Exception.Message
        Close-DevicePort
    }
})
$form.Add_FormClosed({
    $timer.Stop()
    Close-DevicePort
    $script:brushInk.Dispose()
    $script:brushCard.Dispose()
    $script:brushPick.Dispose()
})
Show-Page 'overview'
$timer.Start()
$form.Add_Shown({ if ($ports.SelectedItem -eq 'COM7') { $connect.PerformClick() } })
[System.Windows.Forms.Application]::Run($form)
