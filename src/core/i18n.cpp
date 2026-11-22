#include "i18n.h"

#include <QCoreApplication>
#include <QFont>
#include <QGuiApplication>
#include <QHash>

namespace
{
struct Entry
{
    const char *english;
    const char *chinese;
};

// English is the source language; the key is the English text itself.
// Add a column here to support another language.
const Entry kEntries[] = {
    // ---- application shell -------------------------------------------------
    {"Ollama Launcher", "Ollama Launcher"},
    {"Start, stop and monitor your local Ollama service",
     "本地 Ollama 服务的启停与运行监控"},
    {"Ready", "就绪"},
    {"Running", "运行中"},
    {"Stopped", "已停止"},
    {"running", "运行中"},
    {"stopped", "已停止"},
    {"Not running", "未运行"},
    {"Uptime %1m %2s", "已运行 %1 分 %2 秒"},
    {"Logs", "运行日志"},
    {"Clients", "连接程序"},
    {"Running models", "运行中模型"},
    {"Settings", "设置"},
    {"Settings · Ollama Launcher", "设置 · Ollama Launcher"},
    {"No system tray detected. The app runs as a normal window.",
     "未检测到系统托盘，程序将以普通窗口模式运行。"},
    {"Minimized to the tray. Double-click the tray icon to reopen.",
     "已隐藏到系统托盘，双击托盘图标可重新打开。"},
    {"Ollama service error", "Ollama 服务异常"},

    // ---- service control ---------------------------------------------------
    {"Service control", "服务控制"},
    {"▶  Start Ollama", "▶  启动 Ollama"},
    {"■  Stop Ollama", "■  停止 Ollama"},
    {"Copy service URL", "复制服务地址"},
    {"Copy client URL", "复制客户端地址"},
    {"Service URL: http://%1:%2", "服务地址：http://%1:%2"},
    {"Client URL (stats proxy): http://%1:%2", "客户端地址（统计代理）：http://%1:%2"},
    {"Client URL: the stats proxy is disabled, connect to the service URL",
     "客户端地址：统计代理未启用，请直连服务地址"},
    {"Tip: point your client's base URL at the client URL to collect real token and TTFT "
     "statistics. Without the proxy, clients talk to Ollama directly.",
     "提示：把客户端 base URL 指向「客户端地址」即可统计真实 token 与 TTFT；"
     "不启用代理时客户端直连服务地址。"},
    {"ollama serve started", "ollama serve 已启动"},

    // ---- metrics -----------------------------------------------------------
    {"Installed models", "已安装模型"},
    {"Process memory / private (MB)", "进程内存 / 私有(MB)"},
    {"Model VRAM (MB)", "模型显存(MB)"},
    {"Generated tokens", "累计生成 Tokens"},
    {"First token latency (ms)", "首 Token 延迟(ms)"},
    {"Generation speed (tok/s)", "生成速度(tok/s)"},
    {"Client connections", "客户端连接数"},
    {"Errors / warnings", "错误 / 警告"},
    {"Last poll: %1 · %2", "最近采集：%1 · %2"},
    {"Last poll: %1 · Ollama %2", "最近采集：%1 · Ollama %2"},
    {"Monitor poll failed: ", "监控采集失败："},

    // ---- benchmark ---------------------------------------------------------
    {"Model benchmark", "模型测速"},
    {"Pick a locally installed model to measure TTFT", "选择本地已安装的模型进行 TTFT 测速"},
    {"(select a model)", "（请选择模型）"},
    {"Benchmark now (TTFT)", "立即测速 (TTFT)"},
    {"Benchmarking...", "测速中…"},
    {"Benchmarking", "测速中"},
    {"Benchmark cancelled (Ollama stopped)", "测速已取消（Ollama 服务已停止）"},
    {"Benchmarking TTFT with model %1...", "正在用模型 %1 测速 TTFT…"},
    {"Benchmarking: loading the model and generating the first token...",
     "测速中：正在加载模型并生成首个 token，请稍候…"},
    {"Benchmark failed: ", "测速失败："},
    {"Benchmark failed: Ollama is not running", "测速失败：Ollama 服务未运行"},
    {"Benchmark failed: select a model first", "测速失败：请先选择模型"},
    {"Ollama is not running. Start the service before benchmarking.",
     "Ollama 服务未运行，请先启动服务再测速。"},
    {"Select a locally installed model in the benchmark list first.",
     "请先在「模型测速」中选择一个本地已安装的模型。"},
    {"The selected model is invalid.", "所选模型无效。"},
    {"No model available to benchmark", "没有可用模型，无法测速"},
    {"No valid data (the model did not load or the response was empty). Check the log.",
     "未取得有效数据（模型未成功加载或响应为空），请查看运行日志。"},
    {"No valid tokens received (HTTP %1, %2 bytes)%3",
     "未获取到有效 token（HTTP %1，响应 %2 字节）%3"},
    {"Service returned an error: ", "服务返回错误："},
    {"Result: TTFT=%1ms, %2 tokens, %3 tok/s", "测速结果：TTFT=%1ms，生成 %2 token，%3 tok/s"},

    // ---- logs --------------------------------------------------------------
    {"All levels", "全部日志"},
    {"Info and above", "信息以上"},
    {"Warnings and above", "警告以上"},
    {"Errors only", "仅错误"},
    {"Search (model name, error code, request path...)",
     "搜索（模型名 / 错误码 / 请求路径…）"},
    {"Hide access logs", "隐藏访问日志"},
    {"Hides [GIN] ... GET /api/* monitor probe requests",
     "隐藏 [GIN] ... GET /api/* 这类监控探针请求"},
    {"Follow: locked", "跟随最新：锁定"},
    {"Follow: unlocked", "跟随最新：已解锁"},
    {"Keeps the view at the newest line. Scrolling up unlocks it; click to lock again and "
     "jump to the end.",
     "锁定后新增日志自动滚动到底部；向上滚动会自动解锁，点击可重新锁定并跳到最新。"},
    {"Reset stats", "重置统计"},
    {"Clear logs", "清空日志"},
    {"Export logs", "导出日志"},
    {"Copy selected log lines", "复制所选日志"},
    {"Copy all logs", "复制全部日志"},
    {"Copied %1 log lines", "已复制 %1 条日志"},
    {"Copied all logs", "已复制全部日志"},
    {"Statistics counters were reset.", "统计计数已重置。"},
    {"Text files (*.txt)", "文本文件 (*.txt)"},
    {"Export failed", "导出失败"},

    // ---- tables ------------------------------------------------------------
    {"Process", "程序名称"},
    {"Executable path", "可执行路径"},
    {"Remote address", "远端地址"},
    {"Remote port", "远端端口"},
    {"Model", "模型名称"},
    {"Size (MB)", "大小(MB)"},
    {"VRAM (MB)", "显存(MB)"},
    {"Unload at", "自动卸载时间"},
    {"Established connections are enumerated with the Win32 GetExtendedTcpTable API and "
     "resolve to a process name; the list refreshes on every poll.",
     "通过 Win32 GetExtendedTcpTable 枚举已建立连接并反查进程名，每轮监控自动刷新。"},

    // ---- settings tab ------------------------------------------------------
    {"Configuration", "配置管理"},
    {"All runtime options are edited in the settings dialog. Port and path changes need a "
     "service restart; proxy changes need an app restart.",
     "所有运行参数在设置对话框中编辑。端口 / 路径变更需重启服务，代理变更需重启本程序。"},
    {"⚙  Open settings", "⚙  打开设置"},
    {"View config file (advanced)", "查看配置文件（高级）"},
    {"The config lives next to the executable in ollama_launcher.ini. Nothing is written to "
     "the registry.",
     "配置文件位于 exe 同目录的 ollama_launcher.ini，不写注册表。"},
    {"Settings saved to %1", "设置已保存到 %1"},
    {"Settings saved. Restart the service (and the app for proxy changes) to apply.",
     "设置已保存。端口/路径变更需重启服务，代理变更需重启本程序。"},

    // ---- settings dialog ---------------------------------------------------
    {"Ollama service", "Ollama 服务"},
    {"ollama.exe path", "ollama.exe 路径"},
    {"Browse...", "浏览…"},
    {"Full path of ollama.exe.", "ollama.exe 的完整路径。"},
    {"Executable to launch. Leave it empty to auto-detect a standard install or PATH.",
     "要启动的可执行文件。留空则自动探测标准安装目录或 PATH。"},
    {"Models directory", "模型目录"},
    {"Directory passed to the child process as OLLAMA_MODELS.",
     "作为 OLLAMA_MODELS 传给子进程的目录。"},
    {"Maps to OLLAMA_MODELS. Leave it empty to use Ollama's default directory.",
     "对应 OLLAMA_MODELS。留空则使用 Ollama 默认目录。"},
    {"Listen address / port", "监听地址 / 端口"},
    {"Port", "端口"},
    {"Address Ollama listens on, usually 127.0.0.1.", "Ollama 的监听地址，通常是 127.0.0.1。"},
    {"Ollama port, 11434 by default. It must differ from the proxy port.",
     "Ollama 端口，默认 11434，不能与代理端口相同。"},
    {"Sets OLLAMA_HOST=address:port. Restart the service after changing it.",
     "设置 OLLAMA_HOST=地址:端口。修改后需重启服务。"},
    {"Extra arguments", "附加启动参数"},
    {"e.g. --verbose", "例如：--verbose"},
    {"Appended verbatim to \"ollama serve\". Usually empty.",
     "原样追加到 ollama serve 之后，通常留空。"},
    {"Extra environment variables", "额外环境变量"},
    {"One KEY=VALUE per line, e.g.\nOLLAMA_IGPU_ENABLE=1\nOLLAMA_KEEP_ALIVE=30m",
     "每行一个 KEY=VALUE，例如：\nOLLAMA_IGPU_ENABLE=1\nOLLAMA_KEEP_ALIVE=30m"},
    {"For example OLLAMA_IGPU_ENABLE=1 for an Intel iGPU, or OLLAMA_KEEP_ALIVE=30m.",
     "例如 Intel 核显可设 OLLAMA_IGPU_ENABLE=1，延长模型驻留可设 OLLAMA_KEEP_ALIVE=30m。"},
    {"Stats proxy (token / TTFT)", "统计代理（token / TTFT）"},
    {"Enable proxy", "启用代理"},
    {"Enable the stats proxy (recommended)", "启用统计代理（推荐）"},
    {"Ollama has no cumulative token counter, so the proxy parses responses while forwarding "
     "them. Metrics stay at 0 when disabled.",
     "Ollama 没有累计 token 接口，代理在转发响应的同时解析统计。关闭后相关指标为 0。"},
    {"Proxy port", "代理端口"},
    {"Point the client's base URL to http://127.0.0.1:<port> to collect stats. Restart this "
     "app after changing it.",
     "把客户端 base URL 指向 http://127.0.0.1:<端口> 即可统计。修改后需重启本程序。"},
    {"Interface and logs", "界面与日志"},
    {"Auto start", "自动启动"},
    {"Start ollama serve when the launcher starts", "启动本程序时自动拉起 ollama serve"},
    {"Starting the launcher also starts the Ollama service.",
     "打开启动器时同时启动 Ollama 服务。"},
    {"Tray behaviour", "托盘行为"},
    {"Hide the window to the tray on close", "关闭窗口时隐藏到托盘"},
    {"The close button then only hides the window. Quit from the tray menu.",
     "关闭按钮只隐藏窗口，退出请用托盘右键菜单。"},
    {"Refresh interval (ms)", "刷新间隔(ms)"},
    {"How often models, memory and connections are sampled. Very small values add probe "
     "traffic.",
     "模型、内存与连接的采样间隔。过小会增加探针请求开销。"},
    {"Log level", "日志级别"},
    {"The log tab can filter further by level and keyword.",
     "日志页还可按级别与关键字进一步过滤。"},
    {"Language", "界面语言"},
    {"Applies immediately after saving.", "保存后立即生效。"},
    {"Config file: %1 (nothing is written to the registry). Port and path changes need a "
     "service restart; proxy changes need an app restart.",
     "配置文件：%1（不写注册表）。端口/路径变更需重启服务，代理变更需重启本程序。"},
    {"Save", "保存"},
    {"Cancel", "取消"},
    {"Select ollama.exe", "选择 ollama.exe"},
    {"Executable (*.exe);;All files (*.*)", "可执行文件 (*.exe);;所有文件 (*.*)"},
    {"Select the models directory", "选择模型目录"},
    {"Environment variable format error", "环境变量格式错误"},
    {"Use the KEY=VALUE format: ", "请按 KEY=VALUE 格式填写："},
    {"Configuration validation failed", "配置校验失败"},

    // ---- tray --------------------------------------------------------------
    {"Show / hide window", "显示 / 隐藏主界面"},
    {"Start Ollama", "启动 Ollama"},
    {"Stop Ollama", "停止 Ollama"},
    {"Quit Ollama Launcher", "退出 Ollama Launcher"},
    {"Ollama Launcher · %1\nModels %2 / %3 · Clients %4 · Tokens %5",
     "Ollama Launcher · %1\n模型 %2 / %3 · 连接 %4 · Tokens %5"},

    // ---- process / proxy / config messages ---------------------------------
    {"Launcher ready. Config file: %1", "启动器已就绪。配置文件：%1"},
    {"Stats proxy listening on %1:%2. Point clients here to collect token/TTFT stats.",
     "统计代理已监听 %1:%2，客户端连接此端口即可获得 token/TTFT 统计。"},
    {"Stats proxy failed to listen on %1:%2: %3", "统计代理监听 %1:%2 失败：%3"},
    {"Proxy forward failed: ", "代理转发失败："},
    {"No ollama.exe path configured.", "未配置 ollama.exe 路径。"},
    {"No ollama.exe path configured. Set it in Settings.", "未配置 ollama.exe 路径，请在设置中指定。"},
    {"ollama.exe was not found in PATH.", "PATH 中找不到 ollama.exe。"},
    {"ollama.exe does not exist: ", "ollama.exe 不存在："},
    {"The listen address cannot be empty.", "监听地址不能为空。"},
    {"The port must be between 1 and 65535.", "端口必须在 1-65535 之间。"},
    {"The proxy port must be between 1 and 65535.", "代理端口必须在 1-65535 之间。"},
    {"The proxy port must differ from the Ollama port.", "代理端口不能与 Ollama 端口相同。"},
    {"The refresh interval must be at least 500 ms.", "刷新间隔不能小于 500 毫秒。"},
    {"Not supported on non-Windows platforms", "非 Windows 平台不支持"},
    {"ollama serve is already running.", "ollama serve 已在运行。"},
    {"Failed to start: %1", "启动失败：%1"},
    {"Started ollama serve (PID=%1)", "已启动 ollama serve（PID=%1）"},
    {"Stopping ollama serve (PID=%1)...", "正在停止 ollama serve（PID=%1）…"},
    {"Kill process tree returned %1: %2", "结束进程树返回 %1：%2"},
    {"ollama serve stopped (exit=%1).", "ollama serve 已停止（exit=%1）。"},
    {"ollama serve exited (exit=%1, status=%2)", "ollama serve 已退出（exit=%1，status=%2）"},
    {"ollama serve exited unexpectedly (exit=%1). If you did not stop it, check the log above.",
     "ollama serve 意外退出（exit=%1）。若不是手动停止，请查看上方日志。"},
    {"Cannot start ollama.exe: ", "无法启动 ollama.exe："},
    {"ollama.exe terminated abnormally: ", "ollama.exe 异常终止："},
};

QHash<QString, QString> buildDictionary()
{
    QHash<QString, QString> dictionary;
    const int count = int(sizeof(kEntries) / sizeof(kEntries[0]));
    dictionary.reserve(count);
    for (int i = 0; i < count; ++i) {
        const Entry &entry = kEntries[i];
        dictionary.insert(QString::fromUtf8(entry.english), QString::fromUtf8(entry.chinese));
    }
    return dictionary;
}

i18n::Language g_language = i18n::Language::English;
} // namespace

namespace i18n
{
Language language()
{
    return g_language;
}

void setLanguage(Language lang)
{
    g_language = lang;
}

QString code(Language lang)
{
    return lang == Language::Chinese ? QStringLiteral("zh") : QStringLiteral("en");
}

Language fromCode(const QString &code)
{
    return code.startsWith(QLatin1String("zh"), Qt::CaseInsensitive) ? Language::Chinese
                                                                    : Language::English;
}

QString uiFontFamily()
{
    // Latin text looks better in Segoe UI; Chinese needs a CJK face with full
    // hinting-free rendering at the sizes used here.
    return g_language == Language::Chinese ? QStringLiteral("Microsoft YaHei")
                                           : QStringLiteral("Segoe UI");
}

QString translate(const QString &english)
{
    if (g_language == Language::English) return english;

    static const QHash<QString, QString> dictionary = buildDictionary();
    return dictionary.value(english, english);
}

void applyFont()
{
    QFont font(uiFontFamily());
    font.setPointSize(9);
    // Disable ClearType hinting: it leaves a grey fringe under the glyphs.
    font.setHintingPreference(QFont::PreferNoHinting);

    if (auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance()))
        app->setFont(font);
}
} // namespace i18n
