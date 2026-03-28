/*

  Copyright (c) 2026 Schildkroet

  This file is part of cangaroo.

  cangaroo is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  cangaroo is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with cangaroo.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "KvaserInterface.h"
#include "KvaserDriver.h"

#include <core/Backend.h>
#include <core/MeasurementInterface.h>
#include <core/CanMessage.h>

// canlib.h is the same header name on Linux and Windows.
// The SDK include path is set via INCLUDEPATH in KvaserDriver.pri.
#include <canlib.h>

#ifndef canINVALID_HANDLE
#  define canINVALID_HANDLE (-1)
#endif

// Kvaser timestamp resolution differs by platform:
//   Linux (linuxcan):  default timer resolution is 1 µs  → divide by 1000 for ms, remainder for µs
//   Windows (canlib):  default timer resolution is 1 ms
#ifdef Q_OS_WIN
static inline void kvaserTimestampToSecUsec(unsigned long ts, long &sec, long &usec)
{
    sec  = ts / 1000;
    usec = (ts % 1000) * 1000;
}
#else
static inline void kvaserTimestampToSecUsec(unsigned long ts, long &sec, long &usec)
{
    sec  = ts / 1000000;
    usec = ts % 1000000;
}
#endif

KvaserInterface::KvaserInterface(KvaserDriver *driver, int channel, QString name)
  : CanInterface((CanDriver *)driver),
    _channel(channel),
    _handle(canINVALID_HANDLE),
    _isOpen(false),
    _name(name),
    _bitrate(500000)
{
    memset(&_stats, 0, sizeof(_stats));
    memset(&_offset_stats, 0, sizeof(_offset_stats));
}

KvaserInterface::~KvaserInterface()
{
    if (_isOpen) {
        close();
    }
}

QString KvaserInterface::getName() const
{
    return _name;
}

void KvaserInterface::setName(QString name)
{
    _name = name;
}

QList<CanTiming> KvaserInterface::getAvailableBitrates()
{
    QList<CanTiming> retval;
    QList<unsigned> bitrates({10000, 20000, 50000, 83333, 100000, 125000, 250000, 500000, 800000, 1000000});
    unsigned i = 0;
    for (unsigned br : bitrates) {
        retval << CanTiming(i++, br, 0, 875);
    }
    return retval;
}

void KvaserInterface::applyConfig(const MeasurementInterface &mi)
{
    if (!mi.doConfigure()) {
        log_info(QString("KvaserInterface %1: not managed by cangaroo, skipping configuration").arg(_name));
        return;
    }

    _bitrate = mi.bitrate();

    // Bitrate is applied in open() via canSetBusParams
    log_info(QString("KvaserInterface %1: configuration stored, bitrate=%2").arg(_name).arg(_bitrate));
}

unsigned KvaserInterface::getBitrate()
{
    return _bitrate;
}

uint32_t KvaserInterface::getCapabilities()
{
    return CanInterface::capability_listen_only;
}

void KvaserInterface::open()
{
    _handle = canOpenChannel(_channel, canOPEN_ACCEPT_VIRTUAL);
    if (_handle < 0) {
        log_error(QString("KvaserInterface %1: canOpenChannel failed: %2").arg(_name).arg(_handle));
        return;
    }

    // Map bitrate to a Kvaser predefined constant or use canSetBusParamsC200 / canSetBusParams
    long freq;
    unsigned int tseg1, tseg2, sjw, noSamp, syncMode;

    switch (_bitrate) {
        case 10000:   freq = canBITRATE_10K;   break;
        case 50000:   freq = canBITRATE_50K;   break;
        case 62500:   freq = canBITRATE_62K;   break;
        case 83333:   freq = canBITRATE_83K;   break;
        case 100000:  freq = canBITRATE_100K;  break;
        case 125000:  freq = canBITRATE_125K;  break;
        case 250000:  freq = canBITRATE_250K;  break;
        case 500000:  freq = canBITRATE_500K;  break;
        case 1000000: freq = canBITRATE_1M;    break;
        default:      freq = canBITRATE_500K;  break;
    }

    canStatus status = canSetBusParams(_handle, freq, 0, 0, 0, 0, 0);
    if (status != canOK) {
        log_error(QString("KvaserInterface %1: canSetBusParams failed: %2").arg(_name).arg(status));
        canClose(_handle);
        _handle = canINVALID_HANDLE;
        return;
    }

    status = canBusOn(_handle);
    if (status != canOK) {
        log_error(QString("KvaserInterface %1: canBusOn failed: %2").arg(_name).arg(status));
        canClose(_handle);
        _handle = canINVALID_HANDLE;
        return;
    }

    _isOpen = true;
    log_info(QString("KvaserInterface %1: opened").arg(_name));
}

bool KvaserInterface::isOpen()
{
    return _isOpen;
}

void KvaserInterface::close()
{
    if (_handle != canINVALID_HANDLE) {
        canBusOff(_handle);
        canClose(_handle);
        _handle = canINVALID_HANDLE;
    }
    _isOpen = false;
    log_info(QString("KvaserInterface %1: closed").arg(_name));
}

void KvaserInterface::sendMessage(const CanMessage &msg)
{
    if (!_isOpen) {
        log_error(QString("KvaserInterface %1: cannot send, interface not open").arg(_name));
        return;
    }

    long id = msg.getId();
    unsigned int flags = 0;

    if (msg.isExtended()) {
        flags |= canMSG_EXT;
    } else {
        flags |= canMSG_STD;
    }

    if (msg.isRTR()) {
        flags |= canMSG_RTR;
    }

    uint8_t dlc = msg.getLength();
    if (dlc > 8) dlc = 8;

    uint8_t data[8] = {};
    if (!msg.isRTR()) {
        for (int i = 0; i < dlc; i++) {
            data[i] = msg.getByte(i);
        }
    }

    canStatus status = canWrite(_handle, id, data, dlc, flags);
    if (status != canOK) {
        log_error(QString("KvaserInterface %1: canWrite failed: %2").arg(_name).arg(status));
        _stats.tx_errors++;
    } else {
        _stats.tx_count++;
        addFrameBits(msg);
    }
}

bool KvaserInterface::readMessage(QList<CanMessage> &msglist, unsigned int timeout_ms)
{
    if (!_isOpen) {
        return false;
    }

    long id;
    uint8_t data[8];
    unsigned int dlc, flags;
    unsigned long timestamp;

    canStatus status = canReadWait(_handle, &id, data, &dlc, &flags, &timestamp, timeout_ms);
    if (status != canOK) {
        return false;
    }

    if (flags & canMSG_ERROR_FRAME) {
        _stats.rx_errors++;
        return false;
    }

    CanMessage msg;
    msg.setId(id & 0x1FFFFFFF);
    msg.setExtended((flags & canMSG_EXT) != 0);
    msg.setRTR((flags & canMSG_RTR) != 0);
    msg.setErrorFrame((flags & canMSG_ERROR_FRAME) != 0);
    msg.setInterfaceId(getId());

    long ts_sec, ts_usec;
    kvaserTimestampToSecUsec(timestamp, ts_sec, ts_usec);
    msg.setTimestamp(ts_sec, ts_usec);

    uint8_t len = (dlc > 8) ? 8 : dlc;
    msg.setLength(len);
    for (int i = 0; i < len; i++) {
        msg.setByte(i, data[i]);
    }

    msglist.append(msg);
    _stats.rx_count++;
    addFrameBits(msg);
    return true;
}

bool KvaserInterface::updateStatistics()
{
    if (!_isOpen) {
        return false;
    }

    // Kvaser does not expose error counters directly via canlib in all versions;
    // keep accumulated software counters from send/receive.
    return true;
}

void KvaserInterface::resetStatistics()
{
    _offset_stats = _stats;
    CanInterface::resetStatistics();
}

uint32_t KvaserInterface::getState()
{
    if (!_isOpen) {
        return state_stopped;
    }

    unsigned long flags = 0;
    canRequestChipStatus(_handle);
    canReadStatus(_handle, &flags);

    if (flags & canSTAT_BUS_OFF)      return state_bus_off;
    if (flags & canSTAT_ERROR_PASSIVE) return state_passive;
    if (flags & canSTAT_ERROR_WARNING) return state_warning;

    return state_ok;
}

int KvaserInterface::getNumRxFrames()
{
    return (int)(_stats.rx_count - _offset_stats.rx_count);
}

int KvaserInterface::getNumRxErrors()
{
    return _stats.rx_errors - _offset_stats.rx_errors;
}

int KvaserInterface::getNumRxOverruns()
{
    return (int)(_stats.rx_overruns - _offset_stats.rx_overruns);
}

int KvaserInterface::getNumTxFrames()
{
    return (int)(_stats.tx_count - _offset_stats.tx_count);
}

int KvaserInterface::getNumTxErrors()
{
    return _stats.tx_errors - _offset_stats.tx_errors;
}

int KvaserInterface::getNumTxDropped()
{
    return (int)(_stats.tx_dropped - _offset_stats.tx_dropped);
}

int KvaserInterface::getChannel() const
{
    return _channel;
}
