#include "updatetask.h"
#include "updatescheduler.h"
#include <QDataStream>
#include <QDebug>
using namespace QtAutoUpdater;

#define NOW QDateTime::currentDateTime()

//-------- TimeSpan --------

TimeSpan::TimeSpan(quint64 count, TimeSpan::TimeUnit unit) :
	count(count),
	unit(unit)
{}

quint64 TimeSpan::msecs() const {
	return this->count * this->unit;
}

QDateTime TimeSpan::addToDateTime(const QDateTime &base) const
{
	switch(this->unit) {
	case MilliSeconds:
		return base.addMSecs((qint64)this->count);
	case Seconds:
		return base.addSecs((qint64)this->count);
	case Minutes:
		return base.addSecs((qint64)this->count * 60ll);
	case Hours:
		return base.addSecs((qint64)this->count * 3600ll);
	case Days:
		return base.addDays((qint64)this->count);
	case Weeks:
		return base.addDays((qint64)this->count * 7ll);
	case Months:
		return base.addMonths((int)this->count);
	case Years:
		return base.addYears((int)this->count);
	default:
		Q_UNREACHABLE();
		return base;
	}
}

QDataStream &operator<<(QDataStream &stream, const QtAutoUpdater::TimeSpan &timeSpan)
{
    stream << timeSpan.count
           << (quint64)timeSpan.unit;
	return stream;
}

QDataStream &operator>>(QDataStream &stream, QtAutoUpdater::TimeSpan &timeSpan)
{
    quint64 tmp;
    stream >> timeSpan.count
           >> tmp;
    timeSpan.unit = (TimeSpan::TimeUnit)tmp;
	return stream;
}

//-------- LoopUpdateTask --------

bool LoopUpdateTask::hasTasks()
{
	if(this->nextPoint.isNull()) {
		this->nextPoint = this->startDelay().addToDateTime(NOW);
		this->repetitionsLeft = this->repetitions();
	}

	if(this->nextPoint > NOW)
		return (this->repetitionsLeft != 0);
	else
		return false;
}

QDateTime LoopUpdateTask::currentTask() const
{
	return this->nextPoint;
}

bool LoopUpdateTask::nextTask()
{
	if(this->repetitionsLeft > 0) {
		if(--this->repetitionsLeft > 0) {
			this->nextPoint = this->pauseSpan().addToDateTime(NOW);
			return true;
		}
	} else if(this->repetitionsLeft < 0)
		return true;

	return false;
}

qint64 LoopUpdateTask::getLeftReps() const
{
	return this->repetitionsLeft;
}

//-------- DelayedLoopUpdateTask --------

BasicLoopUpdateTask::BasicLoopUpdateTask(TimeSpan loopDelta, qint64 repeats) :
	LoopUpdateTask(),
	loopDelta(loopDelta),
	repCount(repeats)
{}

BasicLoopUpdateTask::BasicLoopUpdateTask(const QByteArray &data) :
	LoopUpdateTask(),
	loopDelta(),
	repCount(-1)
{
	QDataStream stream(data);
	stream >> this->loopDelta
		   >> this->repCount;
}

qint64 BasicLoopUpdateTask::repetitions() const
{
	return this->repCount;
}

TimeSpan BasicLoopUpdateTask::pauseSpan() const
{
	return this->loopDelta;
}

QByteArray BasicLoopUpdateTask::store() const
{
	QByteArray data;
	QDataStream stream(&data, QIODevice::WriteOnly);
	stream << this->loopDelta
		   << this->getLeftReps();
	return data;
}

std::type_index BasicLoopUpdateTask::typeIndex() const
{
	return typeid(BasicLoopUpdateTask);
}

//-------- TimePointUpdateTask --------

TimePointUpdateTask::TimePointUpdateTask(const QDateTime &timePoint, TimeSpan::TimeUnit repeatFocus) :
	timePoint(timePoint),
	focusPoint(repeatFocus),
	nextPoint(timePoint)
{}

TimePointUpdateTask::TimePointUpdateTask(const QByteArray &data) :
	timePoint(),
	focusPoint(TimeSpan::MilliSeconds),
	nextPoint()
{
	QDataStream stream(data);
	quint64 tmp;
	stream >> this->timePoint
		   >> tmp;
	this->focusPoint = (TimeSpan::TimeUnit)tmp;
	this->nextPoint = this->timePoint;
}

bool TimePointUpdateTask::hasTasks()
{
	if(this->focusPoint == TimeSpan::MilliSeconds)
		return this->timePoint > NOW;
	else
		return true;
}

QDateTime TimePointUpdateTask::currentTask() const
{
	return this->nextPoint;
}

bool TimePointUpdateTask::nextTask()
{
	switch(this->focusPoint) {
	case TimeSpan::MilliSeconds:
		return false;
	case TimeSpan::Seconds:
		this->nextPoint = this->nextPoint.addSecs(1);
		break;
	case TimeSpan::Minutes:
		this->nextPoint = this->nextPoint.addSecs(60);
		break;
	case TimeSpan::Hours:
		this->nextPoint = this->nextPoint.addSecs(3600);
		break;
	case TimeSpan::Days:
		this->nextPoint = this->nextPoint.addDays(1);
		break;
	case TimeSpan::Weeks:
		this->nextPoint = this->nextPoint.addDays(7);
		break;
	case TimeSpan::Months:
		this->nextPoint = this->nextPoint.addMonths(1);
		break;
	case TimeSpan::Years:
		this->nextPoint = this->nextPoint.addYears(1);
		break;
	default:
		Q_UNREACHABLE();
		return false;
	}

	return true;
}

QByteArray TimePointUpdateTask::store() const
{
	QByteArray data;
	QDataStream stream(&data, QIODevice::WriteOnly);
	stream << this->timePoint
           << (quint64)this->focusPoint;
	return data;
}

std::type_index TimePointUpdateTask::typeIndex() const
{
	return typeid(TimePointUpdateTask);
}

//-------- UpdateTaskList --------

UpdateTaskList::UpdateTaskList() :
	QLinkedList(),
	UpdateTask()
{}

UpdateTaskList::UpdateTaskList(const std::initializer_list<UpdateTask *> &list) :
	QLinkedList(list),
	UpdateTask()
{}

UpdateTaskList::UpdateTaskList(const QByteArray &data) :
	QLinkedList(),
	UpdateTask()
{
	QDataStream stream(data);
	int size;
	stream >> size;
	for(int i = 0; i < size; ++i) {
		QString tInfo;
		stream >> tInfo;

		int taskSize;
		stream >> taskSize;
		if(taskSize > 0) {
			QByteArray taskData(taskSize, Qt::Uninitialized);
			stream.readRawData(taskData.data(), taskSize);
			UpdateTask *task = UpdateScheduler::buildTask(tInfo, taskData);
			if(task)
				this->append(task);
		}
	}
}

bool UpdateTaskList::hasTasks()
{
	if(!this->isEmpty()) {
		if(this->first()->hasTasks())
			return true;
		else
			return this->nextTask();
	} else
		return false;
}

QDateTime UpdateTaskList::currentTask() const
{
	if(!this->isEmpty())
		return this->first()->currentTask();
	else
		return QDateTime();
}

bool UpdateTaskList::nextTask()
{
	if(this->isEmpty())
		return false;

	if(this->first()->nextTask())
		return true;
	else {
		do {
			if(this->first()->hasTasks())
				return true;
			else
				delete this->takeFirst();
		} while(!this->isEmpty());
		return false;
	}
}

QByteArray UpdateTaskList::store() const
{
	QByteArray data;
	QDataStream stream(&data, QIODevice::WriteOnly);
	stream << this->size();
	for(UpdateTask *task : *this) {
		stream << UpdateScheduler::tIndexToInfo(task->typeIndex());
		QByteArray taskData = task->store();
		stream << taskData.size();
		if(taskData.size() > 0)
			stream.writeRawData(taskData.constData(), taskData.size());
	}
	return data;
}

std::type_index UpdateTaskList::typeIndex() const
{
	return typeid(UpdateTaskList);
}
