$ErrorActionPreference = 'Stop'
$secretPath = Join-Path $env:LOCALAPPDATA 'PIXLRenderer\discord-bot-token.dpapi'

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()

if (Test-Path -LiteralPath $secretPath) {
    [System.Windows.Forms.MessageBox]::Show(
        'A PIXL Discord bot credential is already saved for this Windows user.',
        'PIXL Discord setup',
        [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]::Information) | Out-Null
    return
}

$dialog = [System.Windows.Forms.Form]::new()
$dialog.Text = 'PIXL Discord setup'
$dialog.StartPosition = 'CenterScreen'
$dialog.FormBorderStyle = 'FixedDialog'
$dialog.ClientSize = [System.Drawing.Size]::new(490, 168)
$dialog.MaximizeBox = $false
$dialog.MinimizeBox = $false
$dialog.TopMost = $true

$label = [System.Windows.Forms.Label]::new()
$label.Text = 'Paste the Bot Token from Discord Developer Portal > Bot.'
$label.AutoSize = $true
$label.Location = [System.Drawing.Point]::new(18, 20)
$dialog.Controls.Add($label)

$tokenBox = [System.Windows.Forms.TextBox]::new()
$tokenBox.Location = [System.Drawing.Point]::new(18, 52)
$tokenBox.Size = [System.Drawing.Size]::new(454, 25)
$tokenBox.UseSystemPasswordChar = $true
$dialog.Controls.Add($tokenBox)

$note = [System.Windows.Forms.Label]::new()
$note.Text = 'Saved for this Windows account only. Nothing is posted in chat.'
$note.AutoSize = $true
$note.ForeColor = [System.Drawing.Color]::DimGray
$note.Location = [System.Drawing.Point]::new(18, 88)
$dialog.Controls.Add($note)

$saveButton = [System.Windows.Forms.Button]::new()
$saveButton.Text = 'Save token'
$saveButton.Location = [System.Drawing.Point]::new(370, 120)
$saveButton.Size = [System.Drawing.Size]::new(102, 30)
$saveButton.DialogResult = [System.Windows.Forms.DialogResult]::OK
$dialog.Controls.Add($saveButton)
$dialog.AcceptButton = $saveButton

$cancelButton = [System.Windows.Forms.Button]::new()
$cancelButton.Text = 'Cancel'
$cancelButton.Location = [System.Drawing.Point]::new(264, 120)
$cancelButton.Size = [System.Drawing.Size]::new(98, 30)
$cancelButton.DialogResult = [System.Windows.Forms.DialogResult]::Cancel
$dialog.Controls.Add($cancelButton)
$dialog.CancelButton = $cancelButton

try {
    if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK -or
        [string]::IsNullOrWhiteSpace($tokenBox.Text)) {
        return
    }

    $secureToken = ConvertTo-SecureString -String $tokenBox.Text -AsPlainText -Force
    $tokenBox.Clear()
    $secretDirectory = Split-Path -Parent $secretPath
    New-Item -ItemType Directory -Path $secretDirectory -Force | Out-Null
    ConvertFrom-SecureString -SecureString $secureToken |
        Set-Content -LiteralPath $secretPath -Encoding ASCII -NoNewline

    [System.Windows.Forms.MessageBox]::Show(
        'Bot token saved. Return to Codex and say "saved".',
        'PIXL Discord setup',
        [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]::Information) | Out-Null
} finally {
    $tokenBox.Clear()
    $dialog.Dispose()
}
