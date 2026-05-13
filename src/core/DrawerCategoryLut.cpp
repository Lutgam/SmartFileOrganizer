#include "DrawerCategoryLut.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMutex>
#include <QMutexLocker>

namespace {
QMutex g_lutMutex;
SfDrawerCategoryLut g_activeLut = SfDrawerCategoryLut::builtinDefault();
const QString kMiscDrawer = QStringLiteral("📦 雜項");

QStringList parseKeywordArray(const QJsonValue &value)
{
    QStringList out;
    if (!value.isArray())
        return out;
    for (const QJsonValue &v : value.toArray()) {
        const QString s = v.toString().trimmed();
        if (!s.isEmpty())
            out.append(s);
    }
    return out;
}

bool textContainsKeyword(const QString &haystackLower, const QString &keyword)
{
    const QString needle = keyword.trimmed();
    if (needle.isEmpty())
        return false;
    return haystackLower.contains(needle.toLower());
}
} // namespace

void sfSetActiveDrawerCategoryLut(const SfDrawerCategoryLut &lut)
{
    QMutexLocker locker(&g_lutMutex);
    g_activeLut = lut;
}

SfDrawerCategoryLut sfActiveDrawerCategoryLut()
{
    QMutexLocker locker(&g_lutMutex);
    return g_activeLut;
}

void SfDrawerCategoryLut::rebuildDrawerOrderFromKeys()
{
    m_drawerOrder.clear();
    for (auto it = m_keywordsByDrawer.constBegin(); it != m_keywordsByDrawer.constEnd(); ++it) {
        if (!it.key().isEmpty())
            m_drawerOrder.append(it.key());
    }
    if (!m_drawerOrder.contains(kMiscDrawer))
        m_drawerOrder.append(kMiscDrawer);
}

SfDrawerCategoryLut SfDrawerCategoryLut::builtinDefault()
{
    SfDrawerCategoryLut lut;
    lut.m_keywordsByDrawer = {
        {QStringLiteral("🎓 大學學業與通識"),
         {QStringLiteral("作業"), QStringLiteral("報告"), QStringLiteral("筆記"), QStringLiteral("講義"),
          QStringLiteral("課綱"), QStringLiteral("考古題"), QStringLiteral("專題"), QStringLiteral("論文"),
          QStringLiteral("實驗"), QStringLiteral("實習"), QStringLiteral("hw"), QStringLiteral("assignment"),
          QStringLiteral("essay"), QStringLiteral("paper"), QStringLiteral("thesis"), QStringLiteral("syllabus"),
          QStringLiteral("midterm"), QStringLiteral("final"), QStringLiteral("quiz"), QStringLiteral("exam"),
          QStringLiteral("期中"), QStringLiteral("期末"), QStringLiteral("小考"), QStringLiteral("解答")}},
        {QStringLiteral("🔬 STEM與醫學專業"),
         {QStringLiteral("微積分"), QStringLiteral("線性代數"), QStringLiteral("機率"), QStringLiteral("統計"),
          QStringLiteral("物理"), QStringLiteral("化學"), QStringLiteral("生物"), QStringLiteral("力學"),
          QStringLiteral("電磁學"), QStringLiteral("熱力學"), QStringLiteral("解剖"), QStringLiteral("生理"),
          QStringLiteral("藥理"), QStringLiteral("病理"), QStringLiteral("護理"), QStringLiteral("醫學"),
          QStringLiteral("工程"), QStringLiteral("數學")}},
        {QStringLiteral("⚖️ 法商與人文社會"),
         {QStringLiteral("經濟"), QStringLiteral("會計"), QStringLiteral("企管"), QStringLiteral("行銷"),
          QStringLiteral("財政"), QStringLiteral("民法"), QStringLiteral("刑法"), QStringLiteral("商法"),
          QStringLiteral("憲法"), QStringLiteral("歷史"), QStringLiteral("哲學"), QStringLiteral("文學"),
          QStringLiteral("心理學"), QStringLiteral("社會學"), QStringLiteral("藝術"), QStringLiteral("傳播"),
          QStringLiteral("政治")}},
        {QStringLiteral("🤖 AI與資料科學"),
         {QStringLiteral("llama"), QStringLiteral("ggml"), QStringLiteral("gguf"), QStringLiteral("tensor"),
          QStringLiteral("模型"), QStringLiteral("neural"), QStringLiteral("lora"), QStringLiteral("quant"),
          QStringLiteral("weights"), QStringLiteral("pytorch"), QStringLiteral("tensorflow"),
          QStringLiteral("dataset"), QStringLiteral("training"), QStringLiteral("inference"), QStringLiteral("nlp"),
          QStringLiteral("機器學習")}},
        {QStringLiteral("⚙️ 系統與底層開發"),
         {QStringLiteral("cpp"), QStringLiteral("c"), QStringLiteral("rust"), QStringLiteral("go"),
          QStringLiteral("assembly"), QStringLiteral("cu"), QStringLiteral("cuh"), QStringLiteral("wgsl"),
          QStringLiteral("cmake"), QStringLiteral("編譯"), QStringLiteral("cuda"), QStringLiteral("compiler"),
          QStringLiteral("make"), QStringLiteral("build"), QStringLiteral("kernel"), QStringLiteral("linux"),
          QStringLiteral("os"), QStringLiteral("docker"), QStringLiteral("devops")}},
        {QStringLiteral("🌐 網頁與後端開發"),
         {QStringLiteral("svelte"), QStringLiteral("ts"), QStringLiteral("js"), QStringLiteral("html"),
          QStringLiteral("css"), QStringLiteral("react"), QStringLiteral("vue"), QStringLiteral("angular"),
          QStringLiteral("node"), QStringLiteral("npm"), QStringLiteral("django"), QStringLiteral("flask"),
          QStringLiteral("spring"), QStringLiteral("api"), QStringLiteral("rest"), QStringLiteral("frontend"),
          QStringLiteral("backend")}},
        {QStringLiteral("📱 行動與跨平台開發"),
         {QStringLiteral("android"), QStringLiteral("kt"), QStringLiteral("xml"), QStringLiteral("kotlin"),
          QStringLiteral("java"), QStringLiteral("ios"), QStringLiteral("app"), QStringLiteral("mobile"),
          QStringLiteral("swift"), QStringLiteral("flutter"), QStringLiteral("react native"), QStringLiteral("apk"),
          QStringLiteral("ipa"), QStringLiteral("xcode")}},
        {QStringLiteral("✈️ 語文檢定與留學"),
         {QStringLiteral("雅思"), QStringLiteral("IELTS"), QStringLiteral("多益"), QStringLiteral("TOEIC"),
          QStringLiteral("GRE"), QStringLiteral("GMAT"), QStringLiteral("JLPT"), QStringLiteral("真經"),
          QStringLiteral("單字"), QStringLiteral("作文"), QStringLiteral("托福"), QStringLiteral("toefl"),
          QStringLiteral("vocabulary"), QStringLiteral("visa"), QStringLiteral("簽證"), QStringLiteral("留學"),
          QStringLiteral("推薦信"), QStringLiteral("sop"), QStringLiteral("交換學生")}},
        {QStringLiteral("🏖️ 旅遊與生活票證"),
         {QStringLiteral("東京"), QStringLiteral("河口湖"), QStringLiteral("杜拜"), QStringLiteral("機票"),
          QStringLiteral("車票"), QStringLiteral("收據"), QStringLiteral("宿舍"), QStringLiteral("ticket"),
          QStringLiteral("hotel"), QStringLiteral("住宿"), QStringLiteral("訂房"), QStringLiteral("行程"),
          QStringLiteral("tour"), QStringLiteral("travel"), QStringLiteral("護照"), QStringLiteral("passport"),
          QStringLiteral("門票"), QStringLiteral("租約")}},
        {QStringLiteral("💼 商務與職場管理"),
         {QStringLiteral("合約"), QStringLiteral("履歷"), QStringLiteral("辭呈"), QStringLiteral("報價單"),
          QStringLiteral("企劃"), QStringLiteral("計畫"), QStringLiteral("proposal"), QStringLiteral("會議"),
          QStringLiteral("meeting"), QStringLiteral("minutes"), QStringLiteral("專案"), QStringLiteral("project"),
          QStringLiteral("管理"), QStringLiteral("業務"), QStringLiteral("kpi"), QStringLiteral("okr"),
          QStringLiteral("勞健保")}},
        {QStringLiteral("🎨 設計與影音剪輯"),
         {QStringLiteral("素材"), QStringLiteral("設計"), QStringLiteral("design"), QStringLiteral("figma"),
          QStringLiteral("psd"), QStringLiteral("adobe"), QStringLiteral("illustrator"), QStringLiteral("pr"),
          QStringLiteral("ae"), QStringLiteral("字體"), QStringLiteral("font"), QStringLiteral("logo"),
          QStringLiteral("mockup"), QStringLiteral("uiux"), QStringLiteral("剪輯"), QStringLiteral("劇本"),
          QStringLiteral("storyboard"), QStringLiteral("音效")}},
        {QStringLiteral("📝 一般文件與排版"),
         {QStringLiteral("doc"), QStringLiteral("pdf"), QStringLiteral("docx"), QStringLiteral("txt"),
          QStringLiteral("md"), QStringLiteral("markdown"), QStringLiteral("text"), QStringLiteral("草稿"),
          QStringLiteral("draft"), QStringLiteral("心得"), QStringLiteral("範本"), QStringLiteral("template"),
          QStringLiteral("格式"), QStringLiteral("rtf"), QStringLiteral("pages")}},
        {QStringLiteral("🗄️ 數據工程與資料庫"),
         {QStringLiteral("sql"), QStringLiteral("db"), QStringLiteral("database"), QStringLiteral("query"),
          QStringLiteral("etl"), QStringLiteral("pipeline"), QStringLiteral("warehouse"), QStringLiteral("schema"),
          QStringLiteral("mongodb"), QStringLiteral("postgres"), QStringLiteral("mysql"), QStringLiteral("redis"),
          QStringLiteral("kafka"), QStringLiteral("spark"), QStringLiteral("hadoop"), QStringLiteral("table"),
          QStringLiteral("資料庫"), QStringLiteral("數據庫"), QStringLiteral("查詢"), QStringLiteral("nosql"),
          QStringLiteral("xml"), QStringLiteral("json"), QStringLiteral("index"), QStringLiteral("migration")}},
        {QStringLiteral("📊 財務與試算表"),
         {QStringLiteral("xlsx"), QStringLiteral("xls"), QStringLiteral("csv"), QStringLiteral("excel"),
          QStringLiteral("spreadsheet"), QStringLiteral("財報"), QStringLiteral("會計"), QStringLiteral("所得稅"),
          QStringLiteral("試算"), QStringLiteral("報表"), QStringLiteral("pivot"), QStringLiteral("dashboard"),
          QStringLiteral("chart"), QStringLiteral("財務"), QStringLiteral("資產負債"), QStringLiteral("損益"),
          QStringLiteral("budget"), QStringLiteral("invoice"), QStringLiteral("發票"), QStringLiteral("報稅"),
          QStringLiteral("統計"), QStringLiteral("分析")}},
        {QStringLiteral("🌱 農業環境與食品"),
         {QStringLiteral("農業"), QStringLiteral("環境"), QStringLiteral("食品"), QStringLiteral("生態"),
          QStringLiteral("作物"), QStringLiteral("畜牧"), QStringLiteral("organic"), QStringLiteral("farm"),
          QStringLiteral("soil"), QStringLiteral("climate"), QStringLiteral("永續"), QStringLiteral("有機"),
          QStringLiteral("HACCP"), QStringLiteral("食安"), QStringLiteral("林業"), QStringLiteral("漁業"),
          QStringLiteral("agriculture"), QStringLiteral("sustainability")}},
        {QStringLiteral("📚 教育師培與教學"),
         {QStringLiteral("教學"), QStringLiteral("師培"), QStringLiteral("教案"), QStringLiteral("課堂"),
          QStringLiteral("學生"), QStringLiteral("老師"), QStringLiteral("教科"), QStringLiteral("幼教"),
          QStringLiteral("curriculum"), QStringLiteral("pedagogy"), QStringLiteral("特教"), QStringLiteral("學習單"),
          QStringLiteral("評量"), QStringLiteral("級任"), QStringLiteral("課程設計"), QStringLiteral("教師研習"),
          QStringLiteral("lesson"), QStringLiteral("teaching")}},
        {QStringLiteral("🛡️ 軍事與國防政策"),
         {QStringLiteral("軍事"), QStringLiteral("國防"), QStringLiteral("戰略"), QStringLiteral("演習"),
          QStringLiteral("武器"), QStringLiteral("情報"), QStringLiteral("defense"), QStringLiteral("military"),
          QStringLiteral("nato"), QStringLiteral("戰術"), QStringLiteral("安全政策"), QStringLiteral("戰史"),
          QStringLiteral("policy"), QStringLiteral("security")}},
        {QStringLiteral("🎮 遊戲與互動開發"),
         {QStringLiteral("遊戲"), QStringLiteral("game"), QStringLiteral("unity"), QStringLiteral("unreal"),
          QStringLiteral("godot"), QStringLiteral("互動"), QStringLiteral("gameplay"), QStringLiteral("sprite"),
          QStringLiteral("關卡"), QStringLiteral("mod"), QStringLiteral("gamedev"), QStringLiteral("shader"),
          QStringLiteral("npc"), QStringLiteral("rpg"), QStringLiteral("level"), QStringLiteral("quest")}},
        {QStringLiteral("📥 暫存與系統備份"),
         {QStringLiteral("拷貝"), QStringLiteral("複本"), QStringLiteral("未命名"), QStringLiteral("~$"),
          QStringLiteral("test"), QStringLiteral("temp"), QStringLiteral("backup"), QStringLiteral("bak"),
          QStringLiteral("tmp"), QStringLiteral("cache"), QStringLiteral("untitled"), QStringLiteral("copy"),
          QStringLiteral("副本"), QStringLiteral("測試"), QStringLiteral("old"), QStringLiteral("new"),
          QStringLiteral("v1"), QStringLiteral("v2"), QStringLiteral("final"), QStringLiteral("最終版")}},
    };
    lut.rebuildDrawerOrderFromKeys();
    return lut;
}

bool SfDrawerCategoryLut::writeDefaultToFile(const QString &absolutePath)
{
    const SfDrawerCategoryLut def = builtinDefault();
    QJsonObject root;
    for (const QString &drawer : def.m_drawerOrder) {
        if (drawer == kMiscDrawer)
            continue;
        const QStringList kws = def.m_keywordsByDrawer.value(drawer);
        QJsonArray arr;
        for (const QString &kw : kws)
            arr.append(kw);
        root.insert(drawer, arr);
    }

    const QFileInfo fi(absolutePath);
    QDir().mkpath(fi.absolutePath());
    QFile f(absolutePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

SfDrawerCategoryLut SfDrawerCategoryLut::loadFromFile(const QString &absolutePath)
{
    QFile f(absolutePath);
    if (!f.open(QIODevice::ReadOnly))
        return builtinDefault();

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return builtinDefault();

    SfDrawerCategoryLut lut;
    const QJsonObject root = doc.object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        const QString drawer = it.key().trimmed();
        if (drawer.isEmpty() || drawer == kMiscDrawer)
            continue;
        const QStringList kws = parseKeywordArray(it.value());
        if (!kws.isEmpty())
            lut.m_keywordsByDrawer.insert(drawer, kws);
    }
    if (lut.m_keywordsByDrawer.isEmpty())
        return builtinDefault();
    lut.rebuildDrawerOrderFromKeys();
    return lut;
}

QStringList SfDrawerCategoryLut::drawerKeys() const
{
    return m_drawerOrder;
}

QString SfDrawerCategoryLut::matchText(const QString &text) const
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return kMiscDrawer;

    const QString hay = trimmed.toLower();
    for (const QString &drawer : m_drawerOrder) {
        if (drawer == kMiscDrawer)
            continue;
        const QStringList kws = m_keywordsByDrawer.value(drawer);
        for (const QString &kw : kws) {
            if (textContainsKeyword(hay, kw))
                return drawer;
        }
    }
    return kMiscDrawer;
}

QString SfDrawerCategoryLut::normalizeDrawerKey(const QString &raw) const
{
    QString v = raw.trimmed();
    if (v.startsWith(QStringLiteral("SF_DRAWER:")))
        v = v.mid(QStringLiteral("SF_DRAWER:").size()).trimmed();
    if (v.isEmpty() || v == QStringLiteral("__uncat__"))
        return kMiscDrawer;

    for (const QString &ref : m_drawerOrder) {
        if (QString::compare(v, ref, Qt::CaseInsensitive) == 0)
            return ref;
    }

    static const QMap<QString, QString> kLegacyToCanon = {
        {QStringLiteral("💼 工作"), QStringLiteral("💼 商務與職場管理")},
        {QStringLiteral("💼 工作專案"), QStringLiteral("💼 商務與職場管理")},
        {QStringLiteral("📚 學習"), QStringLiteral("🎓 大學學業與通識")},
        {QStringLiteral("📚 學習研究"), QStringLiteral("🎓 大學學業與通識")},
        {QStringLiteral("🎓 大學專業課程"), QStringLiteral("🎓 大學學業與通識")},
        {QStringLiteral("💰 財務"), QStringLiteral("📊 財務與試算表")},
        {QStringLiteral("💰 財務帳務"), QStringLiteral("📊 財務與試算表")},
        {QStringLiteral("🎬 媒體"), QStringLiteral("🎨 設計與影音剪輯")},
        {QStringLiteral("🎬 多媒體"), QStringLiteral("🎨 設計與影音剪輯")},
        {QStringLiteral("🎬 影音多媒體"), QStringLiteral("🎨 設計與影音剪輯")},
        {QStringLiteral("⚙️ 系統"), QStringLiteral("⚙️ 系統與底層開發")},
        {QStringLiteral("⚙️ 系統開發"), QStringLiteral("⚙️ 系統與底層開發")},
        {QStringLiteral("🤖 AI與底層開發"), QStringLiteral("🤖 AI與資料科學")},
        {QStringLiteral("🌐 前端與行動開發"), QStringLiteral("🌐 網頁與後端開發")},
        {QStringLiteral("📝 筆記文件"), QStringLiteral("📝 一般文件與排版")},
        {QStringLiteral("📝 文件與報告"), QStringLiteral("📝 一般文件與排版")},
        {QStringLiteral("🗄️ 數據資料"), QStringLiteral("🗄️ 數據工程與資料庫")},
        {QStringLiteral("🗄️ 數據與報表"), QStringLiteral("📊 財務與試算表")},
        {QStringLiteral("🗄️ 數據與財務報表"), QStringLiteral("📊 財務與試算表")},
        {QStringLiteral("🗓️ 企劃時程"), QStringLiteral("💼 商務與職場管理")},
        {QStringLiteral("🏖️ 生活旅遊與票證"), QStringLiteral("🏖️ 旅遊與生活票證")},
        {QStringLiteral("📥 暫存下載"), QStringLiteral("📥 暫存與系統備份")},
        {QStringLiteral("📥 暫存與備份"), QStringLiteral("📥 暫存與系統備份")},
        {QStringLiteral("[其他雜項]"), kMiscDrawer},
        {QStringLiteral("[未分類雜項]"), kMiscDrawer},
    };
    auto it = kLegacyToCanon.constFind(v);
    if (it != kLegacyToCanon.cend()) {
        for (const QString &ref : m_drawerOrder) {
            if (QString::compare(it.value(), ref, Qt::CaseInsensitive) == 0)
                return ref;
        }
    }

    return kMiscDrawer;
}

bool SfDrawerCategoryLut::isSyntheticDrawerFolderTag(const QString &tag) const
{
    const QString nt = tag.trimmed();
    for (const QString &dk : m_drawerOrder) {
        if (dk == kMiscDrawer)
            continue;
        if (nt == QStringLiteral("[AI] ") + dk)
            return true;
    }
    return false;
}
