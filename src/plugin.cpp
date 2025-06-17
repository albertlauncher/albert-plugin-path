// Copyright (c) 2017-2025 Manuel Schneider

#include "plugin.h"
#include <QDirIterator>
#include <QFileSystemWatcher>
#include <QLabel>
#include <QStringList>
#include <albert/extensionregistry.h>
#include <albert/standarditem.h>
#include <albert/systemutil.h>
#include <functional>
ALBERT_LOGGING_CATEGORY("terminal")
using namespace Qt::StringLiterals;
using namespace albert::util;
using namespace albert;
using namespace std;

static auto getPaths() { return qEnvironmentVariable("PATH").split(u':', Qt::SkipEmptyParts); }

Plugin::Plugin()
{
    indexer_.parallel = [](const bool &abort){
        set<QString> result;
        const auto paths = getPaths();
        DEBG << "Indexing" << paths.join(u", "_s);
        for (const QString &path : paths) {
            QDirIterator dirIt(path, QDir::NoDotAndDotDot|QDir::Files|QDir::Executable, QDirIterator::Subdirectories);
            while (dirIt.hasNext()) {
                if (abort) return result;
                dirIt.next();
                result.insert(dirIt.fileName());
            }
        }
        return result;
    };

    indexer_.finish = [this](set<QString> && res){
        INFO << u"Indexed %1 executables [%2 ms]"_s.arg(res.size()).arg(indexer_.runtime.count());
        index_ = ::move(res);
    };

    watcher_.addPaths(getPaths());
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, [this](){ indexer_.run(); });

    indexer_.run();
}

Plugin::~Plugin() = default;

QWidget *Plugin::buildConfigWidget()
{
    auto t = uR"(<ul style="margin-left:-1em">)"_s;
    for (const auto & path : getPaths())
        t += uR"(<li><a href="file://%1")>%1</a></li>)"_s.arg(path);
    t += u"</ul>"_s;

    auto l = new QLabel(t);
    l->setOpenExternalLinks(true);
    l->setWordWrap(true);
    l->setAlignment(Qt::AlignTop);
    return l;
}

QString Plugin::synopsis(const QString &) const { return tr("<command> [params]"); }

QString Plugin::defaultTrigger() const { return u">"_s; }

vector<Action> Plugin::buildActions(const QString &commandline) const
{
    vector<Action> a;

    a.emplace_back(u"r"_s, tr("Run in terminal"),
                   [=, this]{ apps_->runTerminal(u"%1 ; exec $SHELL"_s.arg(commandline)); });

    a.emplace_back(u"rc"_s, tr("Run in terminal and close on exit"),
                   [=, this]{ apps_->runTerminal(commandline); });

    a.emplace_back(u"rb"_s, tr("Run in background (without terminal)"),
                   [=]{ runDetachedProcess({u"sh"_s, u"-c"_s, commandline}); });

    return a;
}

void Plugin::handleTriggerQuery(Query &query)
{
    if (query.string().trimmed().isEmpty())
        return;

    vector<shared_ptr<Item>> results;

    // Extract data from input string: [0] program. The rest: args
    QString potentialProgram = query.string().section(u' ', 0, 0, QString::SectionSkipEmpty);
    QString remainder = query.string().section(u' ', 1, -1, QString::SectionSkipEmpty);

    static const auto tr_rcmd = tr("Run '%1'");
    static const QStringList icon_urls{u"xdg:utilities-terminal"_s, u"xdg:terminal"_s, u":path"_s};

    QString commonPrefix;
    if (auto it = lower_bound(index_.begin(), index_.end(), potentialProgram); it != index_.end()){
        commonPrefix = *it;

        while (it != index_.end() && it->startsWith(potentialProgram)) {

            // Update common prefix
            auto mismatchindexes = mismatch(it->begin() + potentialProgram.size() - 1, it->end(),
                                            commonPrefix.begin() + potentialProgram.size() - 1);
            commonPrefix.resize(distance(it->begin(), mismatchindexes.first));

            auto commandline = remainder.isEmpty() ? *it : u"%1 %2"_s.arg(*it, remainder);

            results.emplace_back(
                StandardItem::make(
                    {},
                    commandline,
                    tr_rcmd.arg(commandline),
                    icon_urls,
                    buildActions(commandline),
                    commonPrefix
                )
            );
            ++it;
        }

        // Apply completion string to items
        QString completion = u"%1 %2"_s.arg(commonPrefix, remainder);
        for (auto &item: results)
            static_pointer_cast<StandardItem>(item)->setInputActionText(completion);
    }

    // Build generic item

    static const auto tr_title = tr("I'm Feeling Lucky");
    static const auto tr_description = tr("Try running '%1'");
    results.emplace_back(
        StandardItem::make(
            {},
            tr_title,
            tr_description.arg(query.string()),
            icon_urls,
            buildActions(query.string())
        )
    );

    query.add(results);
}
