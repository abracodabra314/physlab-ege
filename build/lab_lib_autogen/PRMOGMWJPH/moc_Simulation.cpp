/****************************************************************************
** Meta object code from reading C++ file 'Simulation.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.12.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/core/Simulation.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'Simulation.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.12.0. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN3lab10SimulationE_t {};
} // unnamed namespace

template <> constexpr inline auto lab::Simulation::qt_create_metaobjectdata<qt_meta_tag_ZN3lab10SimulationE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "lab::Simulation",
        "parametersChanged",
        "",
        "timeChanged",
        "t",
        "runStateChanged",
        "lab::RunState",
        "state",
        "derivationChanged",
        "vectorOptionsChanged",
        "setShowsVelocities",
        "on",
        "setShowsAccelerations",
        "setShowsForces",
        "setVectorScale",
        "scale",
        "reset",
        "start",
        "pause",
        "resume",
        "advance",
        "realSeconds",
        "speed",
        "advanceModelTime",
        "dtModel",
        "seekTo",
        "stepOnce"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'parametersChanged'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'timeChanged'
        QtMocHelpers::SignalData<void(double)>(3, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Double, 4 },
        }}),
        // Signal 'runStateChanged'
        QtMocHelpers::SignalData<void(lab::RunState)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 6, 7 },
        }}),
        // Signal 'derivationChanged'
        QtMocHelpers::SignalData<void()>(8, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'vectorOptionsChanged'
        QtMocHelpers::SignalData<void()>(9, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'setShowsVelocities'
        QtMocHelpers::SlotData<void(bool)>(10, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 11 },
        }}),
        // Slot 'setShowsAccelerations'
        QtMocHelpers::SlotData<void(bool)>(12, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 11 },
        }}),
        // Slot 'setShowsForces'
        QtMocHelpers::SlotData<void(bool)>(13, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 11 },
        }}),
        // Slot 'setVectorScale'
        QtMocHelpers::SlotData<void(double)>(14, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Double, 15 },
        }}),
        // Slot 'reset'
        QtMocHelpers::SlotData<void()>(16, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'start'
        QtMocHelpers::SlotData<void()>(17, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'pause'
        QtMocHelpers::SlotData<void()>(18, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'resume'
        QtMocHelpers::SlotData<void()>(19, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'advance'
        QtMocHelpers::SlotData<void(double, double)>(20, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Double, 21 }, { QMetaType::Double, 22 },
        }}),
        // Slot 'advance'
        QtMocHelpers::SlotData<void(double)>(20, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { QMetaType::Double, 21 },
        }}),
        // Slot 'advanceModelTime'
        QtMocHelpers::SlotData<void(double)>(23, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Double, 24 },
        }}),
        // Slot 'seekTo'
        QtMocHelpers::SlotData<void(double)>(25, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Double, 4 },
        }}),
        // Slot 'stepOnce'
        QtMocHelpers::SlotData<void()>(26, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<Simulation, qt_meta_tag_ZN3lab10SimulationE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject lab::Simulation::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3lab10SimulationE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3lab10SimulationE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN3lab10SimulationE_t>.metaTypes,
    nullptr
} };

void lab::Simulation::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<Simulation *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->parametersChanged(); break;
        case 1: _t->timeChanged((*reinterpret_cast<std::add_pointer_t<double>>(_a[1]))); break;
        case 2: _t->runStateChanged((*reinterpret_cast<std::add_pointer_t<lab::RunState>>(_a[1]))); break;
        case 3: _t->derivationChanged(); break;
        case 4: _t->vectorOptionsChanged(); break;
        case 5: _t->setShowsVelocities((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 6: _t->setShowsAccelerations((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 7: _t->setShowsForces((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 8: _t->setVectorScale((*reinterpret_cast<std::add_pointer_t<double>>(_a[1]))); break;
        case 9: _t->reset(); break;
        case 10: _t->start(); break;
        case 11: _t->pause(); break;
        case 12: _t->resume(); break;
        case 13: _t->advance((*reinterpret_cast<std::add_pointer_t<double>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<double>>(_a[2]))); break;
        case 14: _t->advance((*reinterpret_cast<std::add_pointer_t<double>>(_a[1]))); break;
        case 15: _t->advanceModelTime((*reinterpret_cast<std::add_pointer_t<double>>(_a[1]))); break;
        case 16: _t->seekTo((*reinterpret_cast<std::add_pointer_t<double>>(_a[1]))); break;
        case 17: _t->stepOnce(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (Simulation::*)()>(_a, &Simulation::parametersChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (Simulation::*)(double )>(_a, &Simulation::timeChanged, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (Simulation::*)(lab::RunState )>(_a, &Simulation::runStateChanged, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (Simulation::*)()>(_a, &Simulation::derivationChanged, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (Simulation::*)()>(_a, &Simulation::vectorOptionsChanged, 4))
            return;
    }
}

const QMetaObject *lab::Simulation::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *lab::Simulation::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3lab10SimulationE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int lab::Simulation::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 18)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 18;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 18)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 18;
    }
    return _id;
}

// SIGNAL 0
void lab::Simulation::parametersChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void lab::Simulation::timeChanged(double _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void lab::Simulation::runStateChanged(lab::RunState _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1);
}

// SIGNAL 3
void lab::Simulation::derivationChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void lab::Simulation::vectorOptionsChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}
QT_WARNING_POP
