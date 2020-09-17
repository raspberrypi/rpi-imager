<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="zh_CN">
<context>
    <name>DownloadExtractThread</name>
    <message>
        <location filename="../downloadextractthread.cpp" line="166"/>
        <source>Error writing to storage</source>
        <translation>写入存储时出错</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="188"/>
        <location filename="../downloadextractthread.cpp" line="348"/>
        <source>Error extracting archive: %1</source>
        <translation>解压 %1 时出错</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="234"/>
        <source>Error mounting FAT32 partition</source>
        <translation>挂载FAT32分区错误</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="244"/>
        <source>Operating system did not mount FAT32 partition</source>
        <translation>操作系统未挂载FAT32分区</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="267"/>
        <source>Error changing to directory &apos;%1&apos;</source>
        <translation>进入文件夹 “%1” 错误</translation>
    </message>
</context>
<context>
    <name>DownloadThread</name>
    <message>
        <location filename="../downloadthread.cpp" line="131"/>
        <source>Error running diskpart: %1</source>
        <translation>运行 “diskpart” 命令错误 %1</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="152"/>
        <source>Error removing existing partitions</source>
        <translation>删除现有分区时出错</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="178"/>
        <source>Authentication cancelled</source>
        <translation>认证已取消</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="181"/>
        <source>Error running authopen to gain access to disk device &apos;%1&apos;</source>
        <translation>运行authopen以获得对磁盘设备&apos;%1&apos;的访问权限时出错</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="182"/>
        <source>Please verify if &apos;Raspberry Pi Imager&apos; is allowed access to &apos;removable volumes&apos; in privacy settings (under &apos;files and folders&apos; or alternatively give it &apos;full disk access&apos;).</source>
        <translation>请验证是否在隐私设置中允许“ Raspberry Pi Imager”访问“可移动卷”（在“文件和文件夹”下，或者为它提供“完全磁盘访问”）的权限。</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="203"/>
        <source>Cannot open storage device &apos;%1&apos;.</source>
        <translation>无法打开存储设备&apos;%1&apos;。</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="217"/>
        <source>Write error while zero&apos;ing out MBR</source>
        <translation>将MBR清零时写入错误</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="229"/>
        <source>Write error while trying to zero out last part of card.
Card could be advertising wrong capacity (possible counterfeit)</source>
        <translation>尝试将卡的最后一部分清零时写入错误。
卡的容量可能不正确（可能是扩容假卡）</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="350"/>
        <source>Access denied error while writing file to disk.</source>
        <translation>将文件写入磁盘时访问被拒绝错误。</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="355"/>
        <source>Controlled Folder Access seems to be enabled. Please add both rpi-imager.exe and fat32format.exe to the list of allowed apps and try again.</source>
        <translation>受控文件夹访问似乎已启用。 请将rpi-imager.exe和fat32format.exe都添加到允许的应用程序列表中，然后重试。</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="361"/>
        <source>Error writing file to disk</source>
        <translation>将文件写入磁盘时出错</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="589"/>
        <source>Error writing to storage (while flushing)</source>
        <translation>刷新时写入存储时出错</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="596"/>
        <source>Error writing to storage (while fsync)</source>
        <translation>在fsync时写入存储时出错</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="577"/>
        <source>Download corrupt. Hash does not match</source>
        <translation>下载的文件损坏。 哈希值不匹配</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="630"/>
        <source>Error writing first block (partition table)</source>
        <translation>写入第一个块（分区表）时出错</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="686"/>
        <source>Error reading from storage.
SD card may be broken.</source>
        <translation>从存储读取数据时错误。
SD卡可能已损坏。</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="705"/>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation>验证写入失败。 SD卡的内容与写入的内容不同。</translation>
    </message>
</context>
<context>
    <name>DriveFormatThread</name>
    <message>
        <location filename="../driveformatthread.cpp" line="63"/>
        <location filename="../driveformatthread.cpp" line="124"/>
        <location filename="../driveformatthread.cpp" line="185"/>
        <source>Error partitioning: %1</source>
        <translation>错误分区：%1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="84"/>
        <source>Error starting fat32format</source>
        <translation>启动fat32format命令时出错</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="94"/>
        <source>Error running fat32format: %1</source>
        <translation>运行fat32format时出错：%1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="104"/>
        <source>Error determining new drive letter</source>
        <translation>确定新驱动器号时出错</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="109"/>
        <source>Invalid device: %1</source>
        <translation>无效的设备：%1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="146"/>
        <source>Error formatting (through udisks2)</source>
        <translation>格式化错误</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="174"/>
        <source>Error starting sfdisk</source>
        <translation>启动sfdisk命令时出错</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="196"/>
        <source>Error starting mkfs.fat</source>
        <translation>启动mkfs.fat时出错</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="206"/>
        <source>Error running mkfs.fat: %1</source>
        <translation>运行mkfs.fat时出错：%1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="213"/>
        <source>Formatting not implemented for this platform</source>
        <translation>暂不支持此平台的格式化</translation>
    </message>
</context>
<context>
    <name>ImageWriter</name>
    <message>
        <location filename="../imagewriter.cpp" line="170"/>
        <source>Storage capacity is not large enough.
Needs to be at least %1 GB</source>
        <translation>存储容量不足。
至少需要%1 GB的空白空间</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="176"/>
        <source>Input file is not a valid disk image.
File size %1 bytes is not a multiple of 512 bytes.</source>
        <translation>输入文件不是有效的磁盘映像。
文件大小%1字节不是512字节的倍数。</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="200"/>
        <source>Downloading and writing image</source>
        <translation>下载和写入镜像</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="409"/>
        <source>Select image</source>
        <translation>选择镜像</translation>
    </message>
</context>
<context>
    <name>LocalFileExtractThread</name>
    <message>
        <location filename="../localfileextractthread.cpp" line="38"/>
        <source>Error opening image file</source>
        <translation>打开图像文件时出错</translation>
    </message>
</context>
<context>
    <name>MsgPopup</name>
    <message>
        <location filename="../MsgPopup.qml" line="96"/>
        <source>NO</source>
        <translation>不</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="109"/>
        <source>YES</source>
        <translation>是</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="122"/>
        <source>CONTINUE</source>
        <translation>继续</translation>
    </message>
</context>
<context>
    <name>QObject</name>
    <message>
        <location filename="../linux/linuxdrivelist.cpp" line="111"/>
        <source>Internal SD card reader</source>
        <translation>内置SD卡读卡器</translation>
    </message>
</context>
<context>
    <name>main</name>
    <message>
        <location filename="../main.qml" line="24"/>
        <source>Raspberry Pi Imager v%1</source>
        <translation>树莓派镜像烧录 v%1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="735"/>
        <source>Are you sure you want to quit?</source>
        <translation>你确定你要退出吗？</translation>
    </message>
    <message>
        <location filename="../main.qml" line="736"/>
        <source>Raspberry Pi Imager is still busy.&lt;br&gt;Are you sure you want to quit?</source>
        <translation>Raspberry Pi Imager还未完成任务。&lt;br&gt;您确定要退出吗？</translation>
    </message>
    <message>
        <location filename="../main.qml" line="91"/>
        <location filename="../main.qml" line="304"/>
        <source>Operating System</source>
        <translation>操作系统</translation>
    </message>
    <message>
        <location filename="../main.qml" line="103"/>
        <source>CHOOSE OS</source>
        <translation>选择操作系统</translation>
    </message>
    <message>
        <location filename="../main.qml" line="134"/>
        <location filename="../main.qml" line="588"/>
        <source>SD Card</source>
        <translation>SD卡</translation>
    </message>
    <message>
        <location filename="../main.qml" line="146"/>
        <location filename="../main.qml" line="868"/>
        <source>CHOOSE SD CARD</source>
        <translation>选择SD卡</translation>
    </message>
    <message>
        <location filename="../main.qml" line="176"/>
        <source>WRITE</source>
        <translation>烧录</translation>
    </message>
    <message>
        <location filename="../main.qml" line="753"/>
        <location filename="../main.qml" line="813"/>
        <source>Writing... %1%</source>
        <translation>写入中...%1%</translation>
    </message>
    <message>
        <location filename="../main.qml" line="221"/>
        <source>CANCEL WRITE</source>
        <translation>取消写入</translation>
    </message>
    <message>
        <location filename="../main.qml" line="121"/>
        <source>Select this button to change the operating system</source>
        <translation>更改操作系统</translation>
    </message>
    <message>
        <location filename="../main.qml" line="159"/>
        <source>Select this button to change the destination SD card</source>
        <translation>更改目标SD卡</translation>
    </message>
    <message>
        <location filename="../main.qml" line="181"/>
        <source>Select this button to start writing the image</source>
        <translation>开始刷写</translation>
    </message>
    <message>
        <location filename="../main.qml" line="224"/>
        <location filename="../main.qml" line="810"/>
        <source>Cancelling...</source>
        <translation>取消中...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="236"/>
        <source>CANCEL VERIFY</source>
        <translation>取消验证</translation>
    </message>
    <message>
        <location filename="../main.qml" line="239"/>
        <location filename="../main.qml" line="833"/>
        <location filename="../main.qml" line="886"/>
        <source>Finalizing...</source>
        <translation>完成中...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="385"/>
        <location filename="../main.qml" line="862"/>
        <source>Erase</source>
        <translation>擦除</translation>
    </message>
    <message>
        <location filename="../main.qml" line="386"/>
        <source>Format card as FAT32</source>
        <translation>格式化SD卡为FAT32格式</translation>
    </message>
    <message>
        <location filename="../main.qml" line="393"/>
        <source>Use custom</source>
        <translation>使用自定义镜像</translation>
    </message>
    <message>
        <location filename="../main.qml" line="394"/>
        <source>Select a custom .img from your computer</source>
        <translation>使用下载的系统镜像文件烧录</translation>
    </message>
    <message>
        <location filename="../main.qml" line="480"/>
        <source>Local file</source>
        <translation>本地文件</translation>
    </message>
    <message>
        <location filename="../main.qml" line="693"/>
        <source>[WRITE PROTECTED]</source>
        <translation>[写保护]</translation>
    </message>
    <message>
        <location filename="../main.qml" line="747"/>
        <source>Warning</source>
        <translation>警告</translation>
    </message>
    <message>
        <location filename="../main.qml" line="766"/>
        <source>All existing data on &apos;%1&apos; will be erased.&lt;br&gt;Are you sure you want to continue?</source>
        <translation>&apos;%1&apos;上的所有现有数据将被删除。&lt;br&gt;确定要继续吗？</translation>
    </message>
    <message>
        <location filename="../main.qml" line="863"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been erased&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1 &lt;/ b&gt;已被删除&lt;br&gt; &lt;br&gt;您现在可以从读取器中取出SD卡</translation>
    </message>
    <message>
        <location filename="../main.qml" line="893"/>
        <location filename="../main.qml" line="935"/>
        <source>Error parsing os_list.json</source>
        <translation>解析 os_list.json 错误</translation>
    </message>
    <message>
        <location filename="../main.qml" line="973"/>
        <source>Connect an USB stick containing images first.&lt;br&gt;The images must be located in the root folder of the USB stick.</source>
        <translation>连接包含镜像的U盘。&lt;br&gt;镜像必须位于U盘的根文件夹中。</translation>
    </message>
    <message>
        <location filename="../main.qml" line="988"/>
        <source>SD card is write protected.&lt;br&gt;Push the lock switch on the left side of the card upwards, and try again.</source>
        <translation>SD卡具有写保护。&lt;br&gt;尝试向上推SD卡的左侧的锁定开关，然后重试。</translation>
    </message>
    <message>
        <location filename="../main.qml" line="417"/>
        <source>Back</source>
        <translation>返回</translation>
    </message>
    <message>
        <location filename="../main.qml" line="418"/>
        <source>Go back to main menu</source>
        <translation>回到主页</translation>
    </message>
    <message>
        <location filename="../main.qml" line="475"/>
        <source>Released: %1</source>
        <translation>解压中...%1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="478"/>
        <source>Cached on your computer</source>
        <translation>在你的电脑上缓存</translation>
    </message>
    <message>
        <location filename="../main.qml" line="482"/>
        <source>Online - %1 GB download</source>
        <translation>已下载：%1 GB</translation>
    </message>
    <message>
        <location filename="../main.qml" line="639"/>
        <location filename="../main.qml" line="691"/>
        <location filename="../main.qml" line="697"/>
        <source>Mounted as %1</source>
        <translation>挂载在：%1 上</translation>
    </message>
    <message>
        <source>QUIT APP</source>
        <translation type="vanished">退出</translation>
    </message>
    <message>
        <source>CONTINUE</source>
        <translation type="vanished">继续</translation>
    </message>
    <message>
        <location filename="../main.qml" line="792"/>
        <source>Error downloading OS list from Internet</source>
        <translation>下载镜像列表错误</translation>
    </message>
    <message>
        <location filename="../main.qml" line="836"/>
        <source>Verifying... %1%</source>
        <translation>验证文件中...%1%</translation>
    </message>
    <message>
        <location filename="../main.qml" line="854"/>
        <source>Error</source>
        <translation>错误</translation>
    </message>
    <message>
        <location filename="../main.qml" line="861"/>
        <source>Write Successful</source>
        <translation>烧录成功</translation>
    </message>
    <message>
        <location filename="../main.qml" line="865"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; 已经成功烧录到 &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;上了，你可以卸载SD卡了</translation>
    </message>
</context>
</TS>
