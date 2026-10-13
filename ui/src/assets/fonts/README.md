# 本地字体资源

本目录于 2026-10-12 从 Google Fonts 官方接口收集，运行时通过 `styles/fonts.css` 使用相对路径加载。
Vite 将字形文件纳入 dist 并生成带哈希的资源名，部署时不需要访问 Google Fonts。

- `google-font-1.ttf` 至 `google-font-6.ttf`：Inter，字重依次为 400、500、600、700、800、900；SIL OFL 1.1。
- `google-font-7.ttf`、`google-font-8.ttf`：JetBrains Mono，字重为 400、500；SIL OFL 1.1。
- `material-symbols-outlined.woff2`：Material Symbols Outlined，支持 wght 100–700、FILL 0–1、opsz 20–48、GRAD -50–200；Apache 2.0。

官方来源：

- [文字字体样式](https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700;800;900&family=JetBrains+Mono:wght@400;500&display=swap)
- [图标字体样式](https://fonts.googleapis.com/css2?family=Material+Symbols+Outlined:opsz,wght,FILL,GRAD@20..48,100..700,0..1,-50..200&display=block)
- [Inter 许可证](https://github.com/google/fonts/blob/main/ofl/inter/OFL.txt)
- [JetBrains Mono 许可证](https://github.com/google/fonts/blob/main/ofl/jetbrainsmono/OFL.txt)
- [Material Symbols 许可证](https://github.com/google/material-design-icons/blob/master/LICENSE)

许可证原文保存在本目录，并同步到 `ui/public/third-party-fonts/`，随构建进入部署包。
更新字体时同步维护样式、字重映射、来源与许可证副本。不要把字体更新做成应用启动时的下载。
