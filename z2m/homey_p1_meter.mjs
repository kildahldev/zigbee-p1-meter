import {identify} from 'zigbee-herdsman-converters/lib/modernExtend';
import * as reporting from 'zigbee-herdsman-converters/lib/reporting';
import {Numeric} from 'zigbee-herdsman-converters/lib/exposes';

const access = {STATE: 1};

const fzElectrical = {
    cluster: 'haElectricalMeasurement',
    type: ['attributeReport', 'readResponse'],
    convert: (model, msg, publish, options, meta) => {
        const ep = msg.endpoint;
        const vDiv = ep.getClusterAttributeValue('haElectricalMeasurement', 'acVoltageDivisor') || 10;
        const cDiv = ep.getClusterAttributeValue('haElectricalMeasurement', 'acCurrentDivisor') || 1000;
        const pDiv = ep.getClusterAttributeValue('haElectricalMeasurement', 'acPowerDivisor') || 1;
        const d = msg.data;
        const r = {};

        if (d.rmsVoltage !== undefined) r.voltage_phase_a = d.rmsVoltage / vDiv;
        if (d.rmsVoltagePhB !== undefined) r.voltage_phase_b = d.rmsVoltagePhB / vDiv;
        if (d.rmsVoltagePhC !== undefined) r.voltage_phase_c = d.rmsVoltagePhC / vDiv;

        if (d.rmsCurrent !== undefined) r.current_phase_a = d.rmsCurrent / cDiv;
        if (d.rmsCurrentPhB !== undefined) r.current_phase_b = d.rmsCurrentPhB / cDiv;
        if (d.rmsCurrentPhC !== undefined) r.current_phase_c = d.rmsCurrentPhC / cDiv;

        if (d.activePower !== undefined) r.power_phase_a = d.activePower / pDiv;
        if (d.activePowerPhB !== undefined) r.power_phase_b = d.activePowerPhB / pDiv;
        if (d.activePowerPhC !== undefined) r.power_phase_c = d.activePowerPhC / pDiv;

        if (d.totalActivePower !== undefined) r.power = d.totalActivePower / pDiv;

        return r;
    },
};

const fzMetering = {
    cluster: 'seMetering',
    type: ['attributeReport', 'readResponse'],
    convert: (model, msg, publish, options, meta) => {
        const ep = msg.endpoint;
        const multiplier = ep.getClusterAttributeValue('seMetering', 'multiplier') || 1;
        const divisor = ep.getClusterAttributeValue('seMetering', 'divisor') || 1000;
        const factor = multiplier / divisor;
        const d = msg.data;
        const r = {};

        if (d.currentSummDelivered !== undefined) r.energy = d.currentSummDelivered * factor;
        if (d.currentSummReceived !== undefined) r.produced_energy = d.currentSummReceived * factor;

        return r;
    },
};

const definition = {
    zigbeeModel: ['P1Meter'],
    model: 'P1Meter',
    vendor: 'Homey',
    description: 'Homey Energy Dongle - P1 Smart Meter (Zigbee)',

    ota: true,

    fromZigbee: [fzElectrical, fzMetering],
    toZigbee: [],

    exposes: [
        new Numeric('power', access.STATE).withUnit('W').withDescription('Total active power'),
        new Numeric('power_phase_a', access.STATE).withUnit('W').withDescription('Active power phase A'),
        new Numeric('power_phase_b', access.STATE).withUnit('W').withDescription('Active power phase B'),
        new Numeric('power_phase_c', access.STATE).withUnit('W').withDescription('Active power phase C'),
        new Numeric('voltage_phase_a', access.STATE).withUnit('V').withDescription('Voltage phase A'),
        new Numeric('voltage_phase_b', access.STATE).withUnit('V').withDescription('Voltage phase B'),
        new Numeric('voltage_phase_c', access.STATE).withUnit('V').withDescription('Voltage phase C'),
        new Numeric('current_phase_a', access.STATE).withUnit('A').withDescription('Current phase A'),
        new Numeric('current_phase_b', access.STATE).withUnit('A').withDescription('Current phase B'),
        new Numeric('current_phase_c', access.STATE).withUnit('A').withDescription('Current phase C'),
        new Numeric('energy', access.STATE).withUnit('kWh').withDescription('Total energy consumed'),
        new Numeric('produced_energy', access.STATE).withUnit('kWh').withDescription('Total energy returned'),
    ],

    configure: async (device, coordinatorEndpoint, definition) => {
        const endpoint = device.getEndpoint(1);

        await reporting.readEletricalMeasurementMultiplierDivisors(endpoint);
        await reporting.readMeteringMultiplierDivisor(endpoint);

        await reporting.bind(endpoint, coordinatorEndpoint, [
            'haElectricalMeasurement',
            'seMetering',
        ]);
    },

    extend: [identify()],
    meta: {},
};

export default definition;
