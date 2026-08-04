; QLens Inno Setup 安装脚本（生成 dist\QLens-0.2.1-setup.exe）
; 安装：复制文件 + 写文件关联（HKCU 用户级，免管理员）
; 卸载：自动清理全部注册关联（uninsdeletekey/uninsdeletevalue）——不留空白右键菜单
; 文件类型图标：每扩展独立 ProgID（QLens.JPG/QLens.PNG...）→ DefaultIcon 指向 icons\<EXT>.ico

#define MyAppName "QLens"
#define MyAppVersion "0.2.1"
#define MyAppPublisher "N.T.Black (unknowbug)"
#define MyAppExe "qlens_manager.exe"
#define QVExe "qlens_quickview.exe"

[Setup]
AppId={{8F2B9C1E-6D4A-4E8B-9A3C-QLENS2026}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\QLens
DefaultGroupName={#MyAppName}
OutputDir=dist
OutputBaseFilename=QLens-{#MyAppVersion}-setup
Compression=lzma2
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
UninstallDisplayIcon={app}\{#QVExe}
WizardStyle=modern

[Languages]
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "dist\QLens-{#MyAppVersion}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\{#MyAppName} Manager"; Filename: "{app}\{#MyAppExe}"
Name: "{group}\{#MyAppName} QuickView"; Filename: "{app}\{#QVExe}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName} Manager"; Filename: "{app}\{#MyAppExe}"; Tasks: desktopicon

[Registry]
; ── 主 ProgID（exe 主图标——jxr/wdp 等无专属图标格式用）──
Root: HKCU; Subkey: "Software\Classes\QLensQuickView"; ValueType: string; ValueName: ""; ValueData: "QLens QuickView"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\QLensQuickView\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\{#QVExe}"",0"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\QLensQuickView\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#QVExe}"" ""%1"""; Flags: uninsdeletekey

; ── 每扩展独立 ProgID（QLens.<EXT>）——DefaultIcon 指向 icons\<EXT>.ico 文件类型图标 ──
Root: HKCU; Subkey: "Software\Classes\QLens.JPG\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\icons\JPG.ico"",0"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\QLens.JPG\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#QVExe}"" ""%1"""; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\QLens.PNG\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\icons\PNG.ico"",0"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\QLens.PNG\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#QVExe}"" ""%1"""; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\QLens.GIF\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\icons\GIF.ico"",0"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\QLens.GIF\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#QVExe}"" ""%1"""; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\QLens.BMP\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\icons\BMP.ico"",0"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\QLens.BMP\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#QVExe}"" ""%1"""; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\QLens.WEBP\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\icons\WEBP.ico"",0"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\QLens.WEBP\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#QVExe}"" ""%1"""; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\QLens.HEIF\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\icons\HEIF.ico"",0"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\QLens.HEIF\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#QVExe}"" ""%1"""; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\QLens.AVIF\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\icons\AVIF.ico"",0"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\QLens.AVIF\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#QVExe}"" ""%1"""; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\QLens.SVG\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\icons\SVG.ico"",0"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\QLens.SVG\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#QVExe}"" ""%1"""; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\QLens.TIFF\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\icons\TIFF.ico"",0"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\QLens.TIFF\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#QVExe}"" ""%1"""; Flags: uninsdeletekey

; ── Applications\qlens_quickview.exe（右键"打开方式"显示 + 支持类型 + 默认应用页）──
Root: HKCU; Subkey: "Software\Classes\Applications\qlens_quickview.exe"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "QLens QuickView"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\qlens_quickview.exe\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\{#QVExe}"",0"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\qlens_quickview.exe\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#QVExe}"" ""%1"""; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\qlens_quickview.exe\SupportedTypes"; ValueType: string; ValueName: ".jpg"; ValueData: ""; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\qlens_quickview.exe\SupportedTypes"; ValueType: string; ValueName: ".jpeg"; ValueData: ""; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\qlens_quickview.exe\SupportedTypes"; ValueType: string; ValueName: ".png"; ValueData: ""; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\qlens_quickview.exe\SupportedTypes"; ValueType: string; ValueName: ".gif"; ValueData: ""; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\qlens_quickview.exe\SupportedTypes"; ValueType: string; ValueName: ".bmp"; ValueData: ""; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\qlens_quickview.exe\SupportedTypes"; ValueType: string; ValueName: ".webp"; ValueData: ""; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\qlens_quickview.exe\SupportedTypes"; ValueType: string; ValueName: ".heic"; ValueData: ""; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\qlens_quickview.exe\SupportedTypes"; ValueType: string; ValueName: ".heif"; ValueData: ""; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\qlens_quickview.exe\SupportedTypes"; ValueType: string; ValueName: ".avif"; ValueData: ""; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\qlens_quickview.exe\SupportedTypes"; ValueType: string; ValueName: ".jxr"; ValueData: ""; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\qlens_quickview.exe\SupportedTypes"; ValueType: string; ValueName: ".wdp"; ValueData: ""; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\qlens_quickview.exe\SupportedTypes"; ValueType: string; ValueName: ".svg"; ValueData: ""; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\qlens_quickview.exe\SupportedTypes"; ValueType: string; ValueName: ".tif"; ValueData: ""; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\qlens_quickview.exe\SupportedTypes"; ValueType: string; ValueName: ".tiff"; ValueData: ""; Flags: uninsdeletekey
; Capabilities\FileAssociations（Windows 10/11 默认应用页——值 = 对应格式 ProgID）
Root: HKCU; Subkey: "Software\Classes\Applications\qlens_quickview.exe\Capabilities\FileAssociations"; ValueType: string; ValueName: ".jpg"; ValueData: "QLens.JPG"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\qlens_quickview.exe\Capabilities\FileAssociations"; ValueType: string; ValueName: ".jpeg"; ValueData: "QLens.JPG"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\qlens_quickview.exe\Capabilities\FileAssociations"; ValueType: string; ValueName: ".png"; ValueData: "QLens.PNG"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\qlens_quickview.exe\Capabilities\FileAssociations"; ValueType: string; ValueName: ".gif"; ValueData: "QLens.GIF"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\qlens_quickview.exe\Capabilities\FileAssociations"; ValueType: string; ValueName: ".bmp"; ValueData: "QLens.BMP"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\qlens_quickview.exe\Capabilities\FileAssociations"; ValueType: string; ValueName: ".webp"; ValueData: "QLens.WEBP"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\qlens_quickview.exe\Capabilities\FileAssociations"; ValueType: string; ValueName: ".heic"; ValueData: "QLens.HEIF"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\qlens_quickview.exe\Capabilities\FileAssociations"; ValueType: string; ValueName: ".heif"; ValueData: "QLens.HEIF"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\qlens_quickview.exe\Capabilities\FileAssociations"; ValueType: string; ValueName: ".avif"; ValueData: "QLens.AVIF"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\qlens_quickview.exe\Capabilities\FileAssociations"; ValueType: string; ValueName: ".jxr"; ValueData: "QLensQuickView"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\qlens_quickview.exe\Capabilities\FileAssociations"; ValueType: string; ValueName: ".wdp"; ValueData: "QLensQuickView"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\qlens_quickview.exe\Capabilities\FileAssociations"; ValueType: string; ValueName: ".svg"; ValueData: "QLens.SVG"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\qlens_quickview.exe\Capabilities\FileAssociations"; ValueType: string; ValueName: ".tif"; ValueData: "QLens.TIFF"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\qlens_quickview.exe\Capabilities\FileAssociations"; ValueType: string; ValueName: ".tiff"; ValueData: "QLens.TIFF"; Flags: uninsdeletekey
; RegisteredApplications（默认应用页入口）
Root: HKCU; Subkey: "Software\RegisteredApplications"; ValueType: string; ValueName: "QLens QuickView"; ValueData: "Software\Classes\Applications\qlens_quickview.exe\Capabilities"; Flags: uninsdeletevalue

; ── 各扩展 OpenWithProgids（值 = 对应格式 ProgID；uninsdeletevalue——只删我们加的值）──
Root: HKCU; Subkey: "Software\Classes\.jpg\OpenWithProgids"; ValueType: string; ValueName: "QLens.JPG"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.jpeg\OpenWithProgids"; ValueType: string; ValueName: "QLens.JPG"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.png\OpenWithProgids"; ValueType: string; ValueName: "QLens.PNG"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.gif\OpenWithProgids"; ValueType: string; ValueName: "QLens.GIF"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.bmp\OpenWithProgids"; ValueType: string; ValueName: "QLens.BMP"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.webp\OpenWithProgids"; ValueType: string; ValueName: "QLens.WEBP"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.heic\OpenWithProgids"; ValueType: string; ValueName: "QLens.HEIF"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.heif\OpenWithProgids"; ValueType: string; ValueName: "QLens.HEIF"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.avif\OpenWithProgids"; ValueType: string; ValueName: "QLens.AVIF"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.jxr\OpenWithProgids"; ValueType: string; ValueName: "QLensQuickView"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.wdp\OpenWithProgids"; ValueType: string; ValueName: "QLensQuickView"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.svg\OpenWithProgids"; ValueType: string; ValueName: "QLens.SVG"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.tif\OpenWithProgids"; ValueType: string; ValueName: "QLens.TIFF"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.tiff\OpenWithProgids"; ValueType: string; ValueName: "QLens.TIFF"; ValueData: ""; Flags: uninsdeletevalue

; 卸载后清理（config 若在 exe 旁则由 UninstallDelete 删）
[UninstallDelete]
Type: files; Name: "{app}\qlens_config.ini"
