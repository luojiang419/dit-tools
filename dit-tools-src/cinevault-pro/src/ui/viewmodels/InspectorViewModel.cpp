#include "ui/viewmodels/InspectorViewModel.h"

#include "application/LibraryQueryService.h"

InspectorViewModel::InspectorViewModel(LibraryQueryService *libraryQueryService, QObject *parent)
    : QObject(parent)
    , m_libraryQueryService(libraryQueryService)
{
    clear();
}

QString InspectorViewModel::title() const
{
    return m_title;
}

QString InspectorViewModel::subtitle() const
{
    return m_subtitle;
}

QVariantList InspectorViewModel::details() const
{
    return m_details;
}

void InspectorViewModel::showSource(qint64 sourceRootId)
{
    m_currentSourceRootId = sourceRootId;
    m_currentAssetId = 0;
    const auto state = m_libraryQueryService->buildSourceInspector(sourceRootId);
    m_title = state.title;
    m_subtitle = state.subtitle;
    m_details = state.details;
    emit stateChanged();
}

void InspectorViewModel::showAsset(qint64 assetId)
{
    m_currentAssetId = assetId;
    m_currentSourceRootId = 0;
    const auto state = m_libraryQueryService->buildAssetInspector(assetId);
    m_title = state.title;
    m_subtitle = state.subtitle;
    m_details = state.details;
    emit stateChanged();
}

void InspectorViewModel::reload()
{
    if (m_currentAssetId > 0) {
        showAsset(m_currentAssetId);
        return;
    }
    if (m_currentSourceRootId > 0) {
        showSource(m_currentSourceRootId);
    }
}

void InspectorViewModel::clear()
{
    m_currentSourceRootId = 0;
    m_currentAssetId = 0;
    m_title = QStringLiteral("检查器");
    m_subtitle = QStringLiteral("选择左侧素材源或中间素材查看详情");
    m_details = QVariantList{
        QVariantMap{{QStringLiteral("label"), QStringLiteral("当前状态")}, {QStringLiteral("value"), QStringLiteral("等待选择对象")}},
        QVariantMap{{QStringLiteral("label"), QStringLiteral("提示")}, {QStringLiteral("value"), QStringLiteral("先创建项目，再导入素材源。")}}
    };
    emit stateChanged();
}
