#include "ui/models/JobListModel.h"

#include "shared/Formatters.h"

#include <algorithm>

namespace {
int jobStateDisplayRank(JobState state)
{
    switch (state) {
    case JobState::Running: return 0;
    case JobState::Pending: return 1;
    case JobState::Failed: return 2;
    case JobState::Completed: return 3;
    case JobState::Cancelled: return 4;
    }
    return 5;
}
}

JobListModel::JobListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int JobListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant JobListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return {};
    }

    const auto &item = m_items.at(index.row());
    switch (role) {
    case IdRole: return item.id;
    case TitleRole: return item.title;
    case DetailRole: return item.detail;
    case ProgressRole: return item.progress;
    case StateRole: return static_cast<int>(item.state);
    case StateLabelRole: return Formatters::jobStateLabel(item.state);
    case SourceRootIdRole: return item.sourceRootId;
    case ErrorRole: return item.errorMessage;
    case JobTypeLabelRole: return Formatters::jobTypeLabel(item.type);
    case SubjectKindRole: return item.subject.kind;
    case SubjectKeyRole: return item.subject.key;
    case SubjectNameRole: return item.subject.name;
    case SubjectPathRole: return item.subject.path;
    case SubjectThumbnailPathRole: return item.subject.thumbnailPath;
    case SubjectTypeLabelRole: return item.subject.typeLabel;
    case ProgressLabelRole: return Formatters::jobProgressLabel(item.progressContext);
    case ProgressShortLabelRole: return Formatters::jobProgressShortLabel(item.progressContext);
    case StepLabelRole: return item.progressContext.stepLabel;
    default: return {};
    }
}

QHash<int, QByteArray> JobListModel::roleNames() const
{
    return {
        {IdRole, "jobId"},
        {TitleRole, "title"},
        {DetailRole, "detail"},
        {ProgressRole, "progress"},
        {StateRole, "state"},
        {StateLabelRole, "stateLabel"},
        {SourceRootIdRole, "sourceRootId"},
        {ErrorRole, "errorMessage"},
        {JobTypeLabelRole, "jobTypeLabel"},
        {SubjectKindRole, "subjectKind"},
        {SubjectKeyRole, "subjectKey"},
        {SubjectNameRole, "subjectName"},
        {SubjectPathRole, "subjectPath"},
        {SubjectThumbnailPathRole, "subjectThumbnailPath"},
        {SubjectTypeLabelRole, "subjectTypeLabel"},
        {ProgressLabelRole, "progressLabel"},
        {ProgressShortLabelRole, "progressShortLabel"},
        {StepLabelRole, "stepLabel"}
    };
}

void JobListModel::setItems(const QVector<Job> &items)
{
    beginResetModel();
    m_items = items;
    sortForDisplay(m_items);
    endResetModel();
}

void JobListModel::sortForDisplay(QVector<Job> &items)
{
    std::stable_sort(items.begin(), items.end(), [](const Job &left, const Job &right) {
        const auto leftRank = jobStateDisplayRank(left.state);
        const auto rightRank = jobStateDisplayRank(right.state);
        if (leftRank != rightRank) {
            return leftRank < rightRank;
        }
        if (left.updatedAt.isValid() && right.updatedAt.isValid() && left.updatedAt != right.updatedAt) {
            return left.updatedAt > right.updatedAt;
        }
        return left.id > right.id;
    });
}
