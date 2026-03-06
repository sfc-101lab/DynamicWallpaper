## 环境配置指南（Windows 上搭建编译环境）

### 步骤 1：安装 MSYS2

1. 访问官网：https://www.msys2.org/
2. 下载 **MSYS2 x86_64 Installer**
3. 安装到默认路径（如 `C:\msys64`）

### 步骤 2：更新包数据库

打开 **MSYS2 MinGW x64** 终端（不是 MSYS2 MSYS！），运行：

```bash
pacman -Syu
```

> 如果提示关闭终端，请关闭后重新打开 **MSYS2 MinGW x64**，再运行一次：

```bash
pacman -Su
```

### 步骤 3：安装编译工具链

在 **MSYS2 MinGW x64** 终端中运行：

```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-make mingw-w64-x86_64-pkg-config
```

> 这会安装：
>
> - GCC 编译器（g++）
> - Make 工具
> - pkg-config（用于库查找）

### 步骤 4：验证安装

```bash
g++ --version
```

应看到类似：

```
g++ (Rev1, Built by MSYS2 project) 13.2.0
```

### 步骤 5：编译你的程序

1. 将 `DynamicWallpaper.cpp` 放到任意文件夹（如 `C:\wallpaper\`）
2. 打开 **MSYS2 MinGW x64** 终端
3. 进入目录：

```bash
cd /C/wallpaper
```

4. 执行编译命令：

```bash
g++ -O2 -D_WIN32_WINNT=0x0601 -mwindows -municode DynamicWallpaper.cpp -o DynamicWallpaper.exe -ld2d1 -lwindowscodecs -lole32 -luuid -ldwmapi
```

> ✅ 成功后会生成 `DynamicWallpaper.exe`

### 步骤 6：运行

- 双击 `DynamicWallpaper.exe` 即可启动动态壁纸
- 要关闭：打开任务管理器 → 结束 `DynamicWallpaper.exe` 进程

---

## 📌 使用提示

- **GIF 要求**：建议使用 **非透明背景** 的 GIF（否则边缘可能有黑边）
- **路径注意**：GIF 文件放置路径为 C:\\wallpapers\\animated.gif
- **性能**：Direct2D 使用 GPU 渲染，即使 4K GIF 也能流畅播放

