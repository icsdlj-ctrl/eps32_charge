$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()
$script:port = $null
$script:buffer = ''
$script:lastQuery = [DateTime]::MinValue
$script:deviceIP = ''
$form = New-Object System.Windows.Forms.Form
$form.Text = 'HoloStation USB 配网与天气设置'
$form.Size = New-Object System.Drawing.Size(720,620)
$form.StartPosition = 'CenterScreen'
$form.Font = New-Object System.Drawing.Font('Microsoft YaHei UI',10)
$form.MinimumSize = $form.Size
function Label($text,$x,$y,$w=640,$h=28) {
    $c=New-Object System.Windows.Forms.Label
    $c.Text=$text;$c.SetBounds($x,$y,$w,$h);$form.Controls.Add($c);return $c
}
function Button($text,$x,$y,$w=140) {
    $c=New-Object System.Windows.Forms.Button
    $c.Text=$text;$c.SetBounds($x,$y,$w,34);$form.Controls.Add($c);return $c
}
function TextBox($x,$y,$w=440) {
    $c=New-Object System.Windows.Forms.TextBox
    $c.SetBounds($x,$y,$w,30);$form.Controls.Add($c);return $c
}
$null=Label '1. USB 接好设备，选择串口并连接' 22 16
$ports=New-Object System.Windows.Forms.ComboBox
$ports.SetBounds(22,50,180,30);$ports.DropDownStyle='DropDownList';$form.Controls.Add($ports)
foreach($p in [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object){$null=$ports.Items.Add($p)}
if($ports.Items.Contains('COM6')){$ports.SelectedItem='COM6'}elseif($ports.Items.Count){$ports.SelectedIndex=0}
$connect=Button '连接设备' 214 48
$scan=Button '刷新串口' 366 48
$connection=Label '尚未连接' 22 92
$null=Label '2. 填入 2.4 GHz Wi-Fi（不支持仅 5 GHz 的网络）' 22 131
$null=Label 'Wi-Fi 名称' 22 170 110
$ssid=TextBox 140 168 490
$null=Label 'Wi-Fi 密码' 22 209 110
$password=TextBox 140 207 490
$password.UseSystemPasswordChar=$true
$show=New-Object System.Windows.Forms.CheckBox
$show.Text='显示密码';$show.SetBounds(140,243,120,28);$form.Controls.Add($show)
$show.Add_CheckedChanged({$password.UseSystemPasswordChar=-not $show.Checked})
$save=Button '保存并联网' 282 244 160
$web=Button '打开设备网页' 454 244 176
$null=Label '3. 屏幕与天气' 22 290
$auto=Button '自动轮播' 22 326
$weather=Button '本地天气页' 174 326
$charge=Button '四路功率页' 326 326
$clock=Button '时钟页' 478 326
$refresh=Button '刷新天气' 22 373
$weatherStatus=Label '天气：等待设备连接' 174 373 490 58
$powerStatus=Label '传感器：等待设备连接' 22 432 640 52
$message=Label '配置直接通过 USB 发送；Wi-Fi 密码只保存到设备，不写入电脑文件。' 22 491 650 48
$message.ForeColor=[System.Drawing.Color]::DimGray

function Send-Command($command) {
    if(-not $script:port -or -not $script:port.IsOpen){throw '请先连接设备串口。'}
    $json=ConvertTo-Json -InputObject $command -Depth 10 -Compress
    $script:port.WriteLine($json)
    $script:lastQuery=[DateTime]::Now
}
function Do-Action($action) {
    try{& $action}catch{$message.Text=$_.Exception.Message;$message.ForeColor=[System.Drawing.Color]::Firebrick}
}
function Apply-Status($d) {
    try { [System.IO.File]::WriteAllText((Join-Path $PSScriptRoot 'HoloStation_last_status.json'),(ConvertTo-Json $d -Depth 10), (New-Object System.Text.UTF8Encoding($false))) } catch {}
    if($d.error){$message.Text='设备返回：'+$d.error;return}
    $script:deviceIP=[string]$d.ip
    if($d.config.wifiState -eq 2){$connection.Text='USB 已连接 | Wi-Fi 已联网 | 地址：'+$d.ip}
    else{$connection.Text='USB 已连接 | Wi-Fi 尚未联网（检查名称、密码和 2.4 GHz 信号）'}
    if($d.weather.valid){
        $suffix=if($d.weather.stale){'（缓存）'}else{''}
        $weatherStatus.Text=('{0}：{1} °C，{2}，湿度 {3}%{4}' -f $d.weather.city,$d.weather.temperature,$d.weather.condition,$d.weather.humidity,$suffix)
    }else{$weatherStatus.Text='本地天气：等待 IP 定位和校时，首次获取约需 1 分钟'}
    $channels=@()
    foreach($name in @('A1','C1','C2','C3')){
        $p=$d.$name
        if($p.valid){$channels+=('{0} {1}V / {2}W' -f $name,$p.V,$p.P)}else{$channels+=($name+' 未检测到')}
    }
    $powerStatus.Text='传感器：'+($channels -join '；')
}
$timer=New-Object System.Windows.Forms.Timer
$timer.Interval=200
$timer.Add_Tick({
    if(-not $script:port -or -not $script:port.IsOpen){return}
    try{
        $script:buffer+=$script:port.ReadExisting()
        if($script:buffer.Length -gt 65536){$script:buffer=$script:buffer.Substring($script:buffer.Length-32768)}
        while($script:buffer.Contains("`n")){
            $at=$script:buffer.IndexOf("`n");$line=$script:buffer.Substring(0,$at);$script:buffer=$script:buffer.Substring($at+1)
            $start=$line.IndexOf('HS_REPLY:')
            if($start -ge 0){try{Apply-Status (ConvertFrom-Json $line.Substring($start+9))}catch{}}
        }
        if(([DateTime]::Now-$script:lastQuery).TotalSeconds -ge 5){Send-Command @{getStatus=@{}}}
    }catch{$connection.Text='USB 通信中断，请重新连接';$timer.Stop()}
})
$scan.Add_Click({
    $ports.Items.Clear()
    foreach($p in [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object){$null=$ports.Items.Add($p)}
    if($ports.Items.Contains('COM6')){$ports.SelectedItem='COM6'}elseif($ports.Items.Count){$ports.SelectedIndex=0}
})
$connect.Add_Click({Do-Action {
    if($script:port -and $script:port.IsOpen){$timer.Stop();$script:port.Close();$script:port.Dispose();$script:port=$null;$connect.Text='连接设备';$connection.Text='已断开 USB';return}
    if(-not $ports.SelectedItem){throw '没有检测到串口，请检查 USB 数据线。'}
    $script:port=New-Object System.IO.Ports.SerialPort([string]$ports.SelectedItem,115200,[System.IO.Ports.Parity]::None,8,[System.IO.Ports.StopBits]::One)
    $script:port.Encoding=New-Object System.Text.UTF8Encoding($false)
    $script:port.NewLine="`n";$script:port.DtrEnable=$false;$script:port.RtsEnable=$false
    $script:port.ReadTimeout=500;$script:port.WriteTimeout=1500;$script:port.Open()
    $script:buffer='';$connect.Text='断开设备';$connection.Text='USB 已连接，正在读取状态'
    $timer.Start();Send-Command @{getStatus=@{}}
}})
$save.Add_Click({Do-Action {
    if(-not $ssid.Text){throw '请填写 Wi-Fi 名称。'}
    Send-Command @{setWiFi=@{ssid=$ssid.Text;psw=$password.Text}}
    $password.Clear();$message.ForeColor=[System.Drawing.Color]::DarkGreen;$message.Text='已发送到设备。正在联网，请等待状态更新。'
}})
$auto.Add_Click({Do-Action {Send-Command @{setPage=@{value=0}}}})
$charge.Add_Click({Do-Action {Send-Command @{setPage=@{value=1}}}})
$clock.Add_Click({Do-Action {Send-Command @{setPage=@{value=2}}}})
$weather.Add_Click({Do-Action {Send-Command @{setPage=@{value=3}}}})
$refresh.Add_Click({Do-Action {Send-Command @{refreshWeather=@{}}}})
$web.Add_Click({Do-Action {
    $url=if($script:deviceIP){'http://'+$script:deviceIP+'/'}else{'http://192.168.4.1/'}
    Start-Process $url
}})
$form.Add_FormClosed({$timer.Stop();if($script:port){if($script:port.IsOpen){$script:port.Close()};$script:port.Dispose()}})
$form.Add_Shown({if($ports.SelectedItem -eq 'COM6'){$connect.PerformClick()}})
[System.Windows.Forms.Application]::Run($form)
