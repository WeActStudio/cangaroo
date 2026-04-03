/*

  Copyright (c) 2015, 2016 Hubert Denkmair

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
#include "qdatetime.h"
#include <core/MeasurementInterface.h>
#include <QtSerialPort/QSerialPort>
#include <QtSerialPort/QSerialPortInfo>
#include <QMutex>

// Maximum rx buffer len
#define SLCAN_MTU (1 + 8 + 1 + 128 + 1) // canfd 64 frame plus \r plus some padding
#define SLCAN_STD_ID_LEN 3
#define SLCAN_EH_STD_ID_LEN 2
#define SLCAN_EXT_ID_LEN 8
#define SLCAN_EH_EXT_ID_LEN 4

#define SLCAN_EH_START (0x80)

#define SLCAN_RET_OK '\x0D'
#define SLCAN_RET_ERR '\x07'

#define RXCIRBUF_LEN (SLCAN_MTU*128) // Buffer for received serial data, serviced at 1ms intervals

class SLCANDriver;

typedef struct {
    bool supports_canfd;
    bool supports_timing;
    uint32_t state;
    uint32_t base_freq;
    uint32_t sample_point;
    uint32_t ctrl_mode;
    uint32_t restart_ms;
} can_config_t;

typedef struct {
    uint32_t can_state;

    uint64_t rx_count;
    int rx_errors;
    uint64_t rx_overruns;

    uint64_t tx_count;
    int tx_errors;
    uint64_t tx_dropped;
} can_status_t;

typedef struct {
    uint8_t buf[SLCAN_MTU+1];
    qint64 length;
} can_msg_t;

class SLCANInterface: public CanInterface {
    Q_OBJECT
public:
    enum {
        CANable,
        WeActStudio,
    };
    enum {
        USB2CANFDV1,
        USB2CANFDV2,
    };
public:
    SLCANInterface(SLCANDriver *driver, int index, QString name, bool fd_support, uint32_t manufacturer, uint32_t model);
    virtual ~SLCANInterface();

    QString getDetailsStr() const;
    virtual QString getName() const;
    void setName(QString name);

    virtual QList<CanTiming> getAvailableBitrates();

    virtual void applyConfig(const MeasurementInterface &mi);
    virtual bool readConfig();
    virtual bool readConfigFromLink(struct rtnl_link *link);

    bool supportsTimingConfiguration();
    bool supportsCanFD();
    bool supportsTripleSampling();

    virtual unsigned getBitrate();
    virtual int getSamplePoint();

    virtual unsigned getBitrateFD();
    virtual int getSamplePointFD();

    virtual uint32_t getCapabilities();

	virtual void open();
    virtual void close();
    virtual bool isOpen();

    virtual void sendMessage(const CanMessage &msg);
    virtual bool readMessage(QList<CanMessage> &msglist, unsigned int timeout_ms);

    virtual bool updateStatistics();
    virtual uint32_t getState();
    virtual int getNumRxFrames();
    virtual int getNumRxErrors();
    virtual int getNumRxOverruns();

    virtual int getNumTxFrames();
    virtual int getNumTxErrors();
    virtual int getNumTxDropped();

    virtual QString getVersion();

    int getIfIndex();

private:
    typedef enum {
        ts_mode_SIOCSHWTSTAMP,
        ts_mode_SIOCGSTAMPNS,
        ts_mode_SIOCGSTAMP
    } ts_mode_t;

    uint32_t _manufacturer;
    uint32_t _model;
    QString _version;

    int _idx;
    bool _isOpen;
    bool _isOffline;
    QSerialPort* _serport;
    QList<can_msg_t> _can_msg_queue;
    QList<CanMessage> _can_msg_tx_queue;
    QMutex _serport_mutex;
    QString _name;

    QMutex _rxbuf_mutex;
    MeasurementInterface _settings;

    can_config_t _config;
    can_status_t _status;
    ts_mode_t _ts_mode;

    QDateTime  _readMessage_datetime;
    uint32_t _send_wait_respond;
    QDateTime  _readMessage_datetime_run;

    QByteArray _rx_data;
    uint8_t _rx_frame[SLCAN_MTU];

    typedef enum {
        PARSE_IDLE,
        PARSE_SLCAN,
        PARSE_RAW_LENGTH,
        PARSE_RAW
    } ParseState;
    ParseState _rx_state;
    uint8_t _rx_data_index = 0;
    uint8_t _rx_raw_length = 0;

    #pragma pack(push, 1)
    typedef struct{
        uint16_t std_id;
        uint8_t dlc;
        uint8_t data[64];
    } std_frame_t;

    typedef struct{
        uint32_t ext_id;
        uint8_t dlc;
        uint8_t data[64];
    } ext_frame_t;

    typedef union {
        uint8_t data[64+4+1];
        std_frame_t std_frame ;
        ext_frame_t ext_frame ;
    } can_frame_t;

    typedef struct
    {
        uint8_t header;
        uint8_t length;
        can_frame_t frame;
    } slcan_eh_msg_t;
    #pragma pack(pop)

#define SLCAN_STD_HEADER (uint8_t)('t')
#define SLCAN_EXT_HEADER (uint8_t)('T')
#define SLCAN_STD_REMOTE_HEADER (uint8_t)('r')
#define SLCAN_EXT_REMOTE_HEADER (uint8_t)('R')
#define SLCAN_STD_FD_HEADER (uint8_t)('d')
#define SLCAN_EXT_FD_HEADER (uint8_t)('D')
#define SLCAN_STD_FDBRS_HEADER (uint8_t)('b')
#define SLCAN_EXT_FDBRS_HEADER (uint8_t)('B')

    bool updateStatus();
    int8_t hal_dlc_code_to_bytes(uint8_t hal_dlc_code);
    bool parseMessage(CanMessage &msg, uint8_t *buf, uint8_t len);
    void eh_sendMessage(const CanMessage &msg);
    bool eh_parseMessage(CanMessage &msg, uint8_t *buf);

private slots:
    void handleSerialError(QSerialPort::SerialPortError error);
};
