// zigbee2mqtt external converter for the DFR-RollerBlinds DIY roller blind
// controller (ESP32-C6 Zigbee ROUTER, esp-zigbee 1.6.x). Exposes:
//   - cover (position + open/close/stop) via the standard Window Covering
//     cluster (0x0102). Lift: 0 = open, 100 = closed (ZCL); z2m presents
//     HA cover semantics.
//   - motor_reversed (rw): Window Covering Mode attr (0x0017) bit 0. Flipping
//     it WIPES calibration on the device (by design — direction sense changed).
//   - calibrated (read-only): ConfigStatus (0x0007) bit 0 "Operational".
//     While false the device rejects all motion commands from z2m; calibrate
//     locally with the keypad (hold Fn 3 s; see DEVELOPER_GUIDE.md).
//
// Install (z2m 2.x): copy into the zigbee2mqtt config dir and register under
// `external_converters:` in configuration.yaml, then restart z2m.
//
// OTA: manufacturerCode 0xFEFE + imageType 0x0003 (soil=0x0001, door=0x0002);
// index served from this repo's ota/index.json (see release-ota.yml).

const m = require('zigbee-herdsman-converters/lib/modernExtend');
const exposes = require('zigbee-herdsman-converters/lib/exposes');
const e = exposes.presets;
const ea = exposes.access;

const fzCalibrated = {
    cluster: 'closuresWindowCovering',
    type: ['attributeReport', 'readResponse'],
    convert: (model, msg) => {
        if (msg.data.configStatus !== undefined) {
            return {calibrated: (msg.data.configStatus & 0x01) === 0x01};
        }
    },
};

const fzMotorReversed = {
    cluster: 'closuresWindowCovering',
    type: ['attributeReport', 'readResponse'],
    convert: (model, msg) => {
        if (msg.data.windowCoveringMode !== undefined) {
            return {motor_reversed: (msg.data.windowCoveringMode & 0x01) === 0x01};
        }
    },
};

const tzMotorReversed = {
    key: ['motor_reversed'],
    convertSet: async (entity, key, value, meta) => {
        const mode = value ? 0x01 : 0x00;
        await entity.write('closuresWindowCovering', {windowCoveringMode: mode});
        return {state: {motor_reversed: value}};
    },
    convertGet: async (entity, key, meta) => {
        await entity.read('closuresWindowCovering', ['windowCoveringMode']);
    },
};

const tzCalibrated = {
    key: ['calibrated'],
    convertGet: async (entity, key, meta) => {
        await entity.read('closuresWindowCovering', ['configStatus']);
    },
};

module.exports = [
    {
        zigbeeModel: ['DFR-RollerBlinds'],
        model: 'DFR-RollerBlinds',
        vendor: 'DFRobot-DIY',
        description: 'ESP32-C6 Zigbee-router roller blind controller (DIY)',
        extend: [
            m.windowCovering({controls: ['lift']}),
        ],
        fromZigbee: [fzCalibrated, fzMotorReversed],
        toZigbee: [tzMotorReversed, tzCalibrated],
        exposes: [
            e.binary('calibrated', ea.STATE_GET, true, false)
                .withDescription('Travel limits calibrated; motion commands are rejected until true'),
            e.binary('motor_reversed', ea.ALL, true, false)
                .withDescription('Flip motor direction (install-time; wipes calibration)'),
        ],
        configure: async (device, coordinatorEndpoint) => {
            const ep = device.getEndpoint(1);
            await ep.read('closuresWindowCovering',
                ['configStatus', 'windowCoveringMode', 'currentPositionLiftPercentage']);
        },
        ota: true,
    },
];
