<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="zh_CN">
<context>
    <name>DownloadExtractThread</name>
    <message>
        <location filename="../downloadextractthread.cpp" line="197"/>
        <location filename="../downloadextractthread.cpp" line="386"/>
        <source>Error extracting archive: %1</source>
        <translation>解压至 %1 时发生错误</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="262"/>
        <source>Error mounting FAT32 partition</source>
        <translation>挂载 FAT32 分区时发生错误</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="282"/>
        <source>Operating system did not mount FAT32 partition</source>
        <translation>操作系统未能挂载 FAT32 分区</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="305"/>
        <source>Error changing to directory &apos;%1&apos;</source>
        <translation>进入文件夹 “%1” 时发生错误</translation>
    </message>
</context>
<context>
    <name>DownloadThread</name>
    <message>
        <location filename="../downloadthread.cpp" line="119"/>
        <source>unmounting drive</source>
        <translation>卸载存储设备</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="139"/>
        <source>opening drive</source>
        <translation>打开存储设备</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="167"/>
        <source>Error running diskpart: %1</source>
        <translation>运行 “diskpart” 命令时发生错误：%1</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="188"/>
        <source>Error removing existing partitions</source>
        <translation>删除现有分区时发生错误</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="214"/>
        <source>Authentication cancelled</source>
        <translation>验证已取消</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="217"/>
        <source>Error running authopen to gain access to disk device &apos;%1&apos;</source>
        <translation>在运行 authopen 以获取对磁盘设备 “%1” 的访问权限时发生错误</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="218"/>
        <source>Please verify if &apos;Raspberry Pi Imager&apos; is allowed access to &apos;removable volumes&apos; in privacy settings (under &apos;files and folders&apos; or alternatively give it &apos;full disk access&apos;).</source>
        <translation>请检查，在隐私设置中是否允许树莓派启动盘制作工具（Raspberry Pi Imager）访问“可移除的宗卷”（位于“文件和文件夹”下），或为其授予“完全磁盘访问权限”。</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="240"/>
        <source>Cannot open storage device &apos;%1&apos;.</source>
        <translation>无法打开存储设备 “%1”。</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="282"/>
        <source>discarding existing data on drive</source>
        <translation>清除存储设备上的已有数据</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="302"/>
        <source>zeroing out first and last MB of drive</source>
        <translation>将存储设备的首尾 1 M 清零</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="308"/>
        <source>Write error while zero&apos;ing out MBR</source>
        <translation>清零 MBR 时写入错误</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="320"/>
        <source>Write error while trying to zero out last part of card.&lt;br&gt;Card could be advertising wrong capacity (possible counterfeit).</source>
        <translation>在对设备尾部清零时写入错误&lt;br&gt;SD 卡标称的容量错误（可能是扩容卡）。</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="409"/>
        <source>starting download</source>
        <translation>开始下载</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="469"/>
        <source>Error downloading: %1</source>
        <translation>下载错误，已下载：%1</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="666"/>
        <source>Access denied error while writing file to disk.</source>
        <translation>将文件写入磁盘时被拒绝访问。</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="671"/>
        <source>Controlled Folder Access seems to be enabled. Please add both rpi-imager.exe and fat32format.exe to the list of allowed apps and try again.</source>
        <translation>似乎已启用受控文件夹访问权限。 请将 rpi-imager.exe 和 fat32format.exe 添加至允许的应用程序列表中，然后重试。</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="678"/>
        <source>Error writing file to disk</source>
        <translation>将文件写入磁盘时发生错误</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="700"/>
        <source>Download corrupt. Hash does not match</source>
        <translation>下载的文件已损坏。校验值不匹配</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="712"/>
        <location filename="../downloadthread.cpp" line="764"/>
        <source>Error writing to storage (while flushing)</source>
        <translation>写入存储设备时发生错误</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="719"/>
        <location filename="../downloadthread.cpp" line="771"/>
        <source>Error writing to storage (while fsync)</source>
        <translation>在写入存储设备时（fsync）发生错误</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="754"/>
        <source>Error writing first block (partition table)</source>
        <translation>在写入第一个区块（分区表）时发生错误</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="829"/>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation>读取存储设备时发生错误。&lt;br&gt;SD 卡可能已损坏。</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="848"/>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation>写入校验失败。SD 卡与写入的内容不一致。</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="901"/>
        <source>Customizing image</source>
        <translation>使用自定义镜像</translation>
    </message>
</context>
<context>
    <name>DriveFormatThread</name>
    <message>
        <location filename="../driveformatthread.cpp" line="63"/>
        <location filename="../driveformatthread.cpp" line="124"/>
        <location filename="../driveformatthread.cpp" line="185"/>
        <source>Error partitioning: %1</source>
        <translation>分区时发生错误：%1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="84"/>
        <source>Error starting fat32format</source>
        <translation>fat32format 执行错误</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="94"/>
        <source>Error running fat32format: %1</source>
        <translation>运行 fat32format 时发生错误：%1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="104"/>
        <source>Error determining new drive letter</source>
        <translation>确定新驱动器号时发生错误</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="109"/>
        <source>Invalid device: %1</source>
        <translation>无效的设备：%1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="146"/>
        <source>Error formatting (through udisks2)</source>
        <translation>格式化时发生错误</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="174"/>
        <source>Error starting sfdisk</source>
        <translation>sfdisk 执行时发生错误</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="199"/>
        <source>Partitioning did not create expected FAT partition %1</source>
        <translation>分区操作未能按预期创建 FAT 分区 %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="208"/>
        <source>Error starting mkfs.fat</source>
        <translation>执行 mkfs.fat 时发生错误</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="218"/>
        <source>Error running mkfs.fat: %1</source>
        <translation>运行 mkfs.fat 时发生错误：%1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="225"/>
        <source>Formatting not implemented for this platform</source>
        <translation>暂不支持在该平台上进行格式化</translation>
    </message>
</context>
<context>
    <name>DstPopup</name>
    <message>
        <location filename="../DstPopup.qml" line="24"/>
        <source>Storage</source>
        <translation type="unfinished">储存设备</translation>
    </message>
    <message>
        <location filename="../DstPopup.qml" line="38"/>
        <source>No storage devices found</source>
        <translation type="unfinished">找不到存储设备</translation>
    </message>
    <message>
        <location filename="../DstPopup.qml" line="67"/>
        <source>Exclude System Drives</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../DstPopup.qml" line="94"/>
        <source>gigabytes</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../DstPopup.qml" line="96"/>
        <location filename="../DstPopup.qml" line="152"/>
        <source>Mounted as %1</source>
        <translation type="unfinished">已挂载为：%1</translation>
    </message>
    <message>
        <location filename="../DstPopup.qml" line="139"/>
        <source>GB</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../DstPopup.qml" line="154"/>
        <source>[WRITE PROTECTED]</source>
        <translation type="unfinished">[写保护]</translation>
    </message>
    <message>
        <location filename="../DstPopup.qml" line="156"/>
        <source>SYSTEM</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../DstPopup.qml" line="197"/>
        <source>SD card is write protected.&lt;br&gt;Push the lock switch on the left side of the card upwards, and try again.</source>
        <translation type="unfinished">SD 卡写保护。&lt;br&gt;请尝试向上推动 SD 卡左侧的锁定开关，然后再试一次。</translation>
    </message>
</context>
<context>
    <name>HWListModel</name>
    <message>
        <location filename="../hwlistmodel.cpp" line="111"/>
        <source>CHOOSE DEVICE</source>
        <translation type="unfinished">选择设备</translation>
    </message>
</context>
<context>
    <name>HwPopup</name>
    <message>
        <location filename="../HwPopup.qml" line="27"/>
        <source>Raspberry Pi Device</source>
        <translation type="unfinished">树莓派设备</translation>
    </message>
</context>
<context>
    <name>ImageWriter</name>
    <message>
        <location filename="../imagewriter.cpp" line="301"/>
        <source>Storage capacity is not large enough.&lt;br&gt;Needs to be at least %1 GB.</source>
        <translation>存储容量不足。&lt;br&gt;至少需要 %1 GB 的剩余空间.</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="307"/>
        <source>Input file is not a valid disk image.&lt;br&gt;File size %1 bytes is not a multiple of 512 bytes.</source>
        <translation>所选文件是无效的磁盘镜像。&lt;br&gt;文件大小 %1 B 不是 512 B 的整倍数。</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="716"/>
        <source>Downloading and writing image</source>
        <translation>下载和写入镜像</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="814"/>
        <source>Select image</source>
        <translation>选择镜像</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="989"/>
        <source>Error synchronizing time. Trying again in 3 seconds</source>
        <translation>时间同步错误。将在 3 秒后重试</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="1001"/>
        <source>STP is enabled on your Ethernet switch. Getting IP will take long time.</source>
        <translation>您的以太网交换机启用了 STP。获取 IP 地址可能需要较长时间。</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="1212"/>
        <source>Would you like to prefill the wifi password from the system keychain?</source>
        <translation>您想要通过系统钥匙串访问自动填充 WiFi 密码吗？</translation>
    </message>
</context>
<context>
    <name>LocalFileExtractThread</name>
    <message>
        <location filename="../localfileextractthread.cpp" line="36"/>
        <source>opening image file</source>
        <translation>打开镜像文件</translation>
    </message>
    <message>
        <location filename="../localfileextractthread.cpp" line="41"/>
        <source>Error opening image file</source>
        <translation>读取镜像文件时发生错误</translation>
    </message>
</context>
<context>
    <name>MsgPopup</name>
    <message>
        <location filename="../MsgPopup.qml" line="52"/>
        <source>NO</source>
        <translation>否</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="61"/>
        <source>YES</source>
        <translation>确认</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="70"/>
        <source>CONTINUE</source>
        <translation>继续</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="78"/>
        <source>QUIT</source>
        <translation>退出</translation>
    </message>
</context>
<context>
    <name>OSListModel</name>
    <message>
        <location filename="../oslistmodel.cpp" line="211"/>
        <source>Recommended</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>OSPopup</name>
    <message>
        <location filename="../OSPopup.qml" line="30"/>
        <source>Operating System</source>
        <translation type="unfinished">操作系统</translation>
    </message>
    <message>
        <location filename="../OSPopup.qml" line="92"/>
        <source>Back</source>
        <translation type="unfinished">返回</translation>
    </message>
    <message>
        <location filename="../OSPopup.qml" line="93"/>
        <source>Go back to main menu</source>
        <translation type="unfinished">回到主菜单</translation>
    </message>
    <message>
        <location filename="../OSPopup.qml" line="250"/>
        <source>Released: %1</source>
        <translation type="unfinished">发布时间：%1</translation>
    </message>
    <message>
        <location filename="../OSPopup.qml" line="260"/>
        <source>Cached on your computer</source>
        <translation type="unfinished">已缓存到本地磁盘</translation>
    </message>
    <message>
        <location filename="../OSPopup.qml" line="262"/>
        <source>Local file</source>
        <translation type="unfinished">本地文件</translation>
    </message>
    <message>
        <location filename="../OSPopup.qml" line="263"/>
        <source>Online - %1 GB download</source>
        <translation type="unfinished">需要下载：%1 GB</translation>
    </message>
    <message>
        <location filename="../OSPopup.qml" line="349"/>
        <source>Connect an USB stick containing images first.&lt;br&gt;The images must be located in the root folder of the USB stick.</source>
        <translation type="unfinished">请先接入一款包含镜像的 USB 设备。&lt;br&gt;其镜像必须位于该设备的根路径下。</translation>
    </message>
</context>
<context>
    <name>OptionsGeneralTab</name>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="40"/>
        <source>Set hostname:</source>
        <translation type="unfinished">设置主机名：</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="68"/>
        <source>Set username and password</source>
        <translation type="unfinished">设置用户名及密码</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="81"/>
        <source>Username:</source>
        <translation type="unfinished">用户名：</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="106"/>
        <location filename="../OptionsGeneralTab.qml" line="183"/>
        <source>Password:</source>
        <translation type="unfinished">密码：</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="147"/>
        <source>Configure wireless LAN</source>
        <translation type="unfinished">配置 WLAN</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="161"/>
        <source>SSID:</source>
        <translation type="unfinished">网络名称：</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="212"/>
        <source>Show password</source>
        <translation type="unfinished">显示密码</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="223"/>
        <source>Hidden SSID</source>
        <translation type="unfinished">隐藏的网络</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="234"/>
        <source>Wireless LAN country:</source>
        <translation type="unfinished">WLAN 区域：</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="252"/>
        <source>Set locale settings</source>
        <translation type="unfinished">本地化设置</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="256"/>
        <source>Time zone:</source>
        <translation type="unfinished">时区：</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="275"/>
        <source>Keyboard layout:</source>
        <translation type="unfinished">键盘布局：</translation>
    </message>
</context>
<context>
    <name>OptionsMiscTab</name>
    <message>
        <location filename="../OptionsMiscTab.qml" line="23"/>
        <source>Play sound when finished</source>
        <translation type="unfinished">在完成后播放提示音</translation>
    </message>
    <message>
        <location filename="../OptionsMiscTab.qml" line="27"/>
        <source>Eject media when finished</source>
        <translation type="unfinished">在完成后卸载磁盘</translation>
    </message>
    <message>
        <location filename="../OptionsMiscTab.qml" line="31"/>
        <source>Enable telemetry</source>
        <translation type="unfinished">启用遥测</translation>
    </message>
</context>
<context>
    <name>OptionsPopup</name>
    <message>
        <location filename="../OptionsPopup.qml" line="28"/>
        <source>OS Customization</source>
        <translation>自定义系统配置</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="65"/>
        <source>General</source>
        <translation>通用</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="74"/>
        <source>Services</source>
        <translation>服务</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="80"/>
        <source>Options</source>
        <translation>可选配置</translation>
    </message>
    <message>
        <source>Set hostname:</source>
        <translation type="vanished">设置主机名：</translation>
    </message>
    <message>
        <source>Set username and password</source>
        <translation type="vanished">设置用户名及密码</translation>
    </message>
    <message>
        <source>Username:</source>
        <translation type="vanished">用户名：</translation>
    </message>
    <message>
        <source>Password:</source>
        <translation type="vanished">密码：</translation>
    </message>
    <message>
        <source>Configure wireless LAN</source>
        <translation type="vanished">配置 WLAN</translation>
    </message>
    <message>
        <source>SSID:</source>
        <translation type="vanished">网络名称：</translation>
    </message>
    <message>
        <source>Show password</source>
        <translation type="vanished">显示密码</translation>
    </message>
    <message>
        <source>Hidden SSID</source>
        <translation type="vanished">隐藏的网络</translation>
    </message>
    <message>
        <source>Wireless LAN country:</source>
        <translation type="vanished">WLAN 区域：</translation>
    </message>
    <message>
        <source>Set locale settings</source>
        <translation type="vanished">本地化设置</translation>
    </message>
    <message>
        <source>Time zone:</source>
        <translation type="vanished">时区：</translation>
    </message>
    <message>
        <source>Keyboard layout:</source>
        <translation type="vanished">键盘布局：</translation>
    </message>
    <message>
        <source>Enable SSH</source>
        <translation type="vanished">开启 SSH</translation>
    </message>
    <message>
        <source>Use password authentication</source>
        <translation type="vanished">使用密码登录</translation>
    </message>
    <message>
        <source>Allow public-key authentication only</source>
        <translation type="vanished">仅使用公钥登录</translation>
    </message>
    <message>
        <source>Set authorized_keys for &apos;%1&apos;:</source>
        <translation type="vanished">为用户 “%1” 设置 authorized_keys：</translation>
    </message>
    <message>
        <source>RUN SSH-KEYGEN</source>
        <translation type="vanished">运行 SSH-KEYGEN</translation>
    </message>
    <message>
        <source>Play sound when finished</source>
        <translation type="vanished">在完成后播放提示音</translation>
    </message>
    <message>
        <source>Eject media when finished</source>
        <translation type="vanished">在完成后卸载磁盘</translation>
    </message>
    <message>
        <source>Enable telemetry</source>
        <translation type="vanished">启用遥测</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="123"/>
        <source>SAVE</source>
        <translation>保存</translation>
    </message>
</context>
<context>
    <name>OptionsServicesTab</name>
    <message>
        <location filename="../OptionsServicesTab.qml" line="36"/>
        <source>Enable SSH</source>
        <translation type="unfinished">开启 SSH</translation>
    </message>
    <message>
        <location filename="../OptionsServicesTab.qml" line="52"/>
        <source>Use password authentication</source>
        <translation type="unfinished">使用密码登录</translation>
    </message>
    <message>
        <location filename="../OptionsServicesTab.qml" line="63"/>
        <source>Allow public-key authentication only</source>
        <translation type="unfinished">仅使用公钥登录</translation>
    </message>
    <message>
        <location filename="../OptionsServicesTab.qml" line="74"/>
        <source>Set authorized_keys for &apos;%1&apos;:</source>
        <translation type="unfinished">为用户 “%1” 设置 authorized_keys：</translation>
    </message>
    <message>
        <location filename="../OptionsServicesTab.qml" line="121"/>
        <source>Delete Key</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../OptionsServicesTab.qml" line="140"/>
        <source>RUN SSH-KEYGEN</source>
        <translation type="unfinished">运行 SSH-KEYGEN</translation>
    </message>
    <message>
        <location filename="../OptionsServicesTab.qml" line="150"/>
        <source>Add SSH Key</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>QObject</name>
    <message>
        <location filename="../linux/linuxdrivelist.cpp" line="126"/>
        <source>Internal SD card reader</source>
        <translation>内置 SD 卡读卡器</translation>
    </message>
</context>
<context>
    <name>UseSavedSettingsPopup</name>
    <message>
        <source>Use OS customization?</source>
        <translation type="vanished">使用自定义系统配置？</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="41"/>
        <source>Would you like to apply OS customization settings?</source>
        <translation>您想应用自定义系统配置吗？</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="82"/>
        <source>NO</source>
        <translation>否</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="63"/>
        <source>NO, CLEAR SETTINGS</source>
        <translation>否，并清空所有设置</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="73"/>
        <source>YES</source>
        <translation>确认</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="50"/>
        <source>EDIT SETTINGS</source>
        <translation>编辑设置</translation>
    </message>
</context>
<context>
    <name>main</name>
    <message>
        <location filename="../main.qml" line="27"/>
        <source>Raspberry Pi Imager v%1</source>
        <translation>树莓派启动盘制作工具 v%1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="120"/>
        <source>Raspberry Pi Device</source>
        <translation>树莓派设备</translation>
    </message>
    <message>
        <source>CHOOSE DEVICE</source>
        <translation type="vanished">选择设备</translation>
    </message>
    <message>
        <location filename="../main.qml" line="144"/>
        <source>Select this button to choose your target Raspberry Pi</source>
        <translation>点击此处选择您的树莓派型号</translation>
    </message>
    <message>
        <location filename="../main.qml" line="158"/>
        <source>Operating System</source>
        <translation>操作系统</translation>
    </message>
    <message>
        <location filename="../main.qml" line="169"/>
        <location filename="../main.qml" line="446"/>
        <source>CHOOSE OS</source>
        <translation>选择操作系统</translation>
    </message>
    <message>
        <location filename="../main.qml" line="182"/>
        <source>Select this button to change the operating system</source>
        <translation>点击此处更改操作系统</translation>
    </message>
    <message>
        <location filename="../main.qml" line="196"/>
        <source>Storage</source>
        <translation>储存设备</translation>
    </message>
    <message>
        <location filename="../main.qml" line="331"/>
        <source>Network not ready yet</source>
        <translation>网络不可用</translation>
    </message>
    <message>
        <source>No storage devices found</source>
        <translation type="vanished">找不到存储设备</translation>
    </message>
    <message>
        <location filename="../main.qml" line="207"/>
        <location filename="../main.qml" line="668"/>
        <source>CHOOSE STORAGE</source>
        <translation>选择储存设备</translation>
    </message>
    <message>
        <location filename="../main.qml" line="221"/>
        <source>Select this button to change the destination storage device</source>
        <translation>点击此处更改所选存储设备</translation>
    </message>
    <message>
        <location filename="../main.qml" line="266"/>
        <source>CANCEL WRITE</source>
        <translation>取消写入</translation>
    </message>
    <message>
        <location filename="../main.qml" line="269"/>
        <location filename="../main.qml" line="591"/>
        <source>Cancelling...</source>
        <translation>正在取消……</translation>
    </message>
    <message>
        <location filename="../main.qml" line="281"/>
        <source>CANCEL VERIFY</source>
        <translation>取消校验</translation>
    </message>
    <message>
        <location filename="../main.qml" line="284"/>
        <location filename="../main.qml" line="614"/>
        <location filename="../main.qml" line="687"/>
        <source>Finalizing...</source>
        <translation>正在完成...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="293"/>
        <source>Next</source>
        <translation>下一步</translation>
    </message>
    <message>
        <location filename="../main.qml" line="299"/>
        <source>Select this button to start writing the image</source>
        <translation>点击此处开始写入</translation>
    </message>
    <message>
        <location filename="../main.qml" line="321"/>
        <source>Using custom repository: %1</source>
        <translation>使用自定义存储库（repository）：%1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="340"/>
        <source>Keyboard navigation: &lt;tab&gt; navigate to next button &lt;space&gt; press button/select item &lt;arrow up/down&gt; go up/down in lists</source>
        <translation>键盘导航：&lt;tab&gt; 切换选项 &lt;space&gt; 确认/选择后使用 &lt;arrow up/down&gt; 可在列表中上下移动</translation>
    </message>
    <message>
        <location filename="../main.qml" line="361"/>
        <source>Language: </source>
        <translation>语言：</translation>
    </message>
    <message>
        <location filename="../main.qml" line="384"/>
        <source>Keyboard: </source>
        <translation>键盘：</translation>
    </message>
    <message>
        <source>[ All ]</source>
        <translation type="vanished">[ All ]</translation>
    </message>
    <message>
        <source>Back</source>
        <translation type="vanished">返回</translation>
    </message>
    <message>
        <source>Go back to main menu</source>
        <translation type="vanished">回到主菜单</translation>
    </message>
    <message>
        <source>Released: %1</source>
        <translation type="vanished">发布时间：%1</translation>
    </message>
    <message>
        <source>Cached on your computer</source>
        <translation type="vanished">已缓存到本地磁盘</translation>
    </message>
    <message>
        <source>Local file</source>
        <translation type="vanished">本地文件</translation>
    </message>
    <message>
        <source>Online - %1 GB download</source>
        <translation type="vanished">需要下载：%1 GB</translation>
    </message>
    <message>
        <source>Mounted as %1</source>
        <translation type="vanished">已挂载为：%1</translation>
    </message>
    <message>
        <source>[WRITE PROTECTED]</source>
        <translation type="vanished">[写保护]</translation>
    </message>
    <message>
        <location filename="../main.qml" line="485"/>
        <source>Are you sure you want to quit?</source>
        <translation>您确定要退出吗？</translation>
    </message>
    <message>
        <location filename="../main.qml" line="486"/>
        <source>Raspberry Pi Imager is still busy.&lt;br&gt;Are you sure you want to quit?</source>
        <translation>树莓派启动盘制作工具还未完成任务。&lt;br&gt;您确定要退出吗？</translation>
    </message>
    <message>
        <location filename="../main.qml" line="497"/>
        <source>Warning</source>
        <translation>警告</translation>
    </message>
    <message>
        <location filename="../main.qml" line="506"/>
        <source>Preparing to write...</source>
        <translation>准备写入……</translation>
    </message>
    <message>
        <location filename="../main.qml" line="520"/>
        <source>All existing data on &apos;%1&apos; will be erased.&lt;br&gt;Are you sure you want to continue?</source>
        <translation>&apos;%1&apos; 上的已有数据都将被删除。&lt;br&gt;是否继续？</translation>
    </message>
    <message>
        <location filename="../main.qml" line="535"/>
        <source>Update available</source>
        <translation>检测到更新</translation>
    </message>
    <message>
        <location filename="../main.qml" line="536"/>
        <source>There is a newer version of Imager available.&lt;br&gt;Would you like to visit the website to download it?</source>
        <translation>检测到新版树莓派启动盘制作工具。&lt;br&gt; 是否转到网页进行下载？</translation>
    </message>
    <message>
        <location filename="../main.qml" line="594"/>
        <source>Writing... %1%</source>
        <translation>正在写入…… %1%</translation>
    </message>
    <message>
        <location filename="../main.qml" line="617"/>
        <source>Verifying... %1%</source>
        <translation>正在校验…… %1%</translation>
    </message>
    <message>
        <location filename="../main.qml" line="624"/>
        <source>Preparing to write... (%1)</source>
        <translation>准备写入…… (%1)</translation>
    </message>
    <message>
        <location filename="../main.qml" line="644"/>
        <source>Error</source>
        <translation>错误</translation>
    </message>
    <message>
        <location filename="../main.qml" line="651"/>
        <source>Write Successful</source>
        <translation>写入成功</translation>
    </message>
    <message>
        <location filename="../main.qml" line="652"/>
        <location filename="../imagewriter.cpp" line="648"/>
        <source>Erase</source>
        <translation>格式化</translation>
    </message>
    <message>
        <location filename="../main.qml" line="653"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been erased&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1 &lt;/ b&gt; 已成功格式化&lt;br&gt; &lt;br&gt;您现在可以从读卡器上取下 SD 卡了</translation>
    </message>
    <message>
        <location filename="../main.qml" line="660"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; 已成功写入至 &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;，您现在可以从读卡器上取下 SD 卡了</translation>
    </message>
    <message>
        <source>Error parsing os_list.json</source>
        <translation type="vanished">os_list.json 解析错误</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="649"/>
        <source>Format card as FAT32</source>
        <translation>把 SD 卡格式化为 FAT32</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="655"/>
        <source>Use custom</source>
        <translation>使用自定义镜像</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="656"/>
        <source>Select a custom .img from your computer</source>
        <translation>选择本地已有的 .img 文件</translation>
    </message>
    <message>
        <source>Connect an USB stick containing images first.&lt;br&gt;The images must be located in the root folder of the USB stick.</source>
        <translation type="vanished">请先接入一款包含镜像的 USB 设备。&lt;br&gt;其镜像必须位于该设备的根路径下。</translation>
    </message>
    <message>
        <source>SD card is write protected.&lt;br&gt;Push the lock switch on the left side of the card upwards, and try again.</source>
        <translation type="vanished">SD 卡写保护。&lt;br&gt;请尝试向上推动 SD 卡左侧的锁定开关，然后再试一次。</translation>
    </message>
</context>
</TS>
