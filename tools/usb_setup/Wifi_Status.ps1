function Get-WifiLink($status) {
    $address = $null
    $ip = [string]$status.ip
    $connected = $status.config.wifiState -eq 2 -and
        [System.Net.IPAddress]::TryParse($ip, [ref]$address) -and
        $address.AddressFamily -eq [System.Net.Sockets.AddressFamily]::InterNetwork -and
        $ip -ne '0.0.0.0'
    return [pscustomobject]@{ Connected = $connected; IP = $ip; SSID = [string]$status.ssid }
}

function Get-WifiFailure($status) {
    if ($status.appHeld) { return '设备应用已停止，请先恢复运行。' }
    if ($status.wifiFail) { return [string]$status.wifiFail }
    switch ([int]$status.wifiReason) {
        0 { return '' }
        200 { return '热点信号中断，请把设备靠近路由器。' }
        201 { return '找不到热点，请检查 Wi-Fi 名称和 2.4 GHz 信号。' }
        202 { return '认证失败，请核对 Wi-Fi 密码和路由器安全模式。' }
        203 { return '找到热点但无法关联，请检查路由器设置。' }
        204 { return '握手超时，请核对密码和信号强度。' }
        default { return ('Wi-Fi 连接失败（原因码 {0}）。' -f $status.wifiReason) }
    }
}

function Get-WifiSetupOutcome($status, [string]$target, [bool]$expired) {
    $link = Get-WifiLink $status
    if ($status.error) {
        $detail = switch ($status.error) {
            'ESP_ERR_NOT_FOUND' { '设备没有保存过这个网络的密码，请填写密码。' }
            'ESP_ERR_INVALID_ARG' { '设备拒绝了 Wi-Fi 名称或密码，请核对输入。' }
            default { '设备返回：' + $status.error }
        }
        return @{ State = 'Failed'; Detail = $detail }
    }
    if ($link.Connected -and $link.SSID -ceq $target) {
        return @{ State = 'Connected'; Detail = '已连接 ' + $target + '，IP：' + $link.IP }
    }
    if ($link.Connected -and -not $link.SSID) {
        return @{ State = 'ConnectedUnknownSSID'; Detail = '设备已联网，IP：' + $link.IP + '（此固件不返回 Wi-Fi 名称）' }
    }
    if ($expired) {
        $detail = Get-WifiFailure $status
        if ($link.Connected) { $detail = '设备仍连接到 ' + $link.SSID + '，没有切换到 ' + $target + '。' }
        if (-not $detail) { $detail = '设备尚未取得 IP，请检查 2.4 GHz 信号、密码和路由器 DHCP。' }
        return @{ State = 'Failed'; Detail = $detail }
    }
    return @{ State = 'Pending'; Detail = '正在连接 ' + $target + '，等待路由器分配 IP…' }
}
