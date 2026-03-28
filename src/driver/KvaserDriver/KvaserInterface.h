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

#pragma once

#include "../CanInterface.h"

// canHandle is typedef int on both Linux and Windows CANlib SDKs.
// We use int here to avoid pulling canlib.h into every translation unit
// that includes this header; canlib.h is included only in the .cpp files.
using canHandle = int;

class KvaserDriver;

class KvaserInterface : public CanInterface {
public:
    KvaserInterface(KvaserDriver *driver, int channel, QString name);
    virtual ~KvaserInterface();

    virtual QString getName() const;
    void setName(QString name);

    virtual QList<CanTiming> getAvailableBitrates();

    virtual void applyConfig(const MeasurementInterface &mi);

    virtual unsigned getBitrate();
    virtual uint32_t getCapabilities();

    virtual void open();
    virtual bool isOpen();
    virtual void close();

    virtual void sendMessage(const CanMessage &msg);
    virtual bool readMessage(QList<CanMessage> &msglist, unsigned int timeout_ms);

    virtual bool updateStatistics();
    virtual void resetStatistics();
    virtual uint32_t getState();
    virtual int getNumRxFrames();
    virtual int getNumRxErrors();
    virtual int getNumRxOverruns();
    virtual int getNumTxFrames();
    virtual int getNumTxErrors();
    virtual int getNumTxDropped();

    int getChannel() const;

private:
    int _channel;
    canHandle _handle;
    bool _isOpen;
    QString _name;
    unsigned _bitrate;

    struct {
        uint64_t rx_count;
        int      rx_errors;
        uint64_t rx_overruns;
        uint64_t tx_count;
        int      tx_errors;
        uint64_t tx_dropped;
    } _stats, _offset_stats;
};
