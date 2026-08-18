# SimConnect SDK（第三方）

本目录镜像了 Microsoft Flight Simulator 2020 的 **SimConnect SDK** 中 C++ 桌面程序编译
所需的文件，供无 SDK 环境（如 CI）构建使用：

```text
include/SimConnect.h       微软官方头文件
lib/SimConnect.lib         动态链接导入库
lib/SimConnect.dll         SimConnect 运行时（构建后复制到 exe 旁）
lib/static/SimConnect.lib  静态链接库
```

## 来源与许可

- 文件版权归 **Microsoft Corporation** 所有，来自 MSFS 2020 SDK
  （`GewoonJaap/Flight-Simulator-Better-Traffic` 镜像，其对应官方路径
  `MSFS SDK\SimConnect SDK\include`、`MSFS SDK\SimConnect SDK\lib`）。
- 若你已安装官方 MSFS SDK，构建时会优先使用你的
  `SIMCONNECT_SDK_PATH`；本目录仅作为 CI / 无 SDK 环境的回退。

## 使用

```cmake
# 显式指定官方 SDK（可选）
cmake -B build -DSIMCONNECT_SDK_PATH="D:/MSFS SDK/SimConnect SDK"
# 或不指定，自动使用本目录
cmake -B build
```
