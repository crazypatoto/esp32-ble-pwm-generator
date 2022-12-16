const serviceUUID = '59d5b564-5077-4ad5-b14c-fc00d5d929c8'
const settingCharacteristicUUID = 'a7016028-0c96-4171-874c-f730b9b952d5'
const readingCharacteristicUUID = 'bc7d3919-54ca-4259-980b-f3269a5621c0'

var mySettingCharacteristic;
var myReadingCharacteristic;
var pwmFrequencySetting;
var pwmDutySetting;
var pwmFrequencyReading;
var pwmDutyReading;
var autoUpdate = false;
var autoUpdateInterval;

function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

function log(data) {
    $('#log-output').append(data + '<br>');
    console.log(data);
}

$(document).ready(function () {
    navigator.bluetooth.getAvailability()
        .then(isBluetoothAvailable => {
            if (isBluetoothAvailable == false) {
                $('#connectBtn').text('Bluetooth is not available!').button("refresh");
                $('.btn').attr('disabled', 'disabled');
            }
        });
});

$('#connectBtn').click(function () {
    $('#log-output').text('');
    connect();
});

$('#saveBtn').click(function () {
    updateSettings();
    log('> Updated and Saved to EEPROM!')
});

$('#autoUpdateCheck').click(function () {
    autoUpdate = $('#autoUpdateCheck').is(':checked');
    if (autoUpdate) {
        $('#saveBtn').text('Disable Auto Update to Save Settings').button("refresh");
        $('#saveBtn').attr('disabled', 'disabled');
        autoUpdateInterval = setInterval(updateSettings, 20);
    } else {
        $('#saveBtn').text('Apply & Save to EEPROM').button("refresh");
        $('#saveBtn').removeAttr('disabled');
        clearInterval(autoUpdateInterval);
    }
});

function connect() {
    connected = false

    let serviceUuid = serviceUUID
    if (serviceUuid.startsWith('0x')) {
        serviceUuid = parseInt(serviceUuid);
    }

    let characteristicUuid = '';
    if (characteristicUuid.startsWith('0x')) {
        characteristicUuid = parseInt(characteristicUuid);
    }

    log('Requesting Bluetooth Device...');
    navigator.bluetooth.requestDevice({ filters: [{ services: [serviceUuid] }] })
        .then(device => {
            log('Connecting to GATT Server...');
            return device.gatt.connect();
        })
        .then(server => {
            log('Getting Service...');
            return server.getPrimaryService(serviceUuid);
        })
        .then(service => {
            // Get all characteristics that match this UUID.
            if (characteristicUuid) {
                log('Getting Specific Characteristics...');
                return service.getCharacteristics(characteristicUuid);
            }
            // Get all characteristics.
            log('Getting All Characteristics...');
            return service.getCharacteristics();
        })
        .then(characteristics => {
            log('> Characteristics: ' +
                characteristics.map(c => c.uuid).join('\n' + ' '.repeat(19)));

            mySettingCharacteristic = characteristics.find(c => c.uuid == settingCharacteristicUUID);
            myReadingCharacteristic = characteristics.find(c => c.uuid == readingCharacteristicUUID);

            if (!mySettingCharacteristic || !myReadingCharacteristic) {
                log('Target Characteristic Not Found, Terminating...');
                return false;
            }

            log('Target Characteristic Found, Initiating...');
            return true;

        })
        .then(_ => {
            log('startNotifications');
            myReadingCharacteristic.startNotifications().then(_ => {
                log('Srart Listening');
                myReadingCharacteristic.addEventListener('characteristicvaluechanged',
                    handleReadingsNotifications);

            });
        })
        .then(_ => {
            setTimeout(function () {
                mySettingCharacteristic.readValue().then(
                    value => {
                        pwmFrequencySetting = value.getUint32(0, true);
                        pwmDutySetting = value.getFloat32(4, true);
                        $('#frequencySetting').val(pwmFrequencySetting);
                        $('#dutySetting').val(pwmDutySetting);
                        $('#dutyRangeSetting').val(pwmDutySetting);
                    }
                );
                $('#connectBtn').text('Connected').button("refresh");
                $('#connectBtn').attr('disabled', 'disabled');
                $('#controlForm').children('fieldset').removeAttr('disabled');
            }, 100);
        })
        .catch(error => {
            log('Error: ' + error);
            return false;
        });
}


function handleReadingsNotifications(event) {
    let value = event.target.value;
    
    // Convert raw data bytes to hex values just for the sake of showing something.
    // In the "real" world, you'd use data.getUint8, data.getUint16 or even
    // TextDecoder to process raw data bytes.
    // let a = [];
    // for (let i = 0; i < value.byteLength; i++) {
    //     a.push('0x' + ('00' + value.getUint8(i).toString(16)).slice(-2));
    // }
    // log('> ' + a.join(' '));

    pwmFrequencyReading = value.getUint32(0, true); // true for little endian
    pwmDutyReading = value.getFloat32(4, true);

    updateUI();
}

function updateUI() {
    $('#frequencyReading').text(pwmFrequencyReading);
    $('#dutyReading').text(pwmDutyReading.toFixed(2) + '%');
    $('#dutyReading').css('width', pwmDutyReading + '%');
}

function validateInt(evt) {
    var theEvent = evt || window.event;

    // Handle paste
    if (theEvent.type === 'paste') {
        key = event.clipboardData.getData('text/plain');
    } else {
        // Handle key press
        var key = theEvent.keyCode || theEvent.which;
        key = String.fromCharCode(key);
    }
    var regex = /[0-9]/;
    if (!regex.test(key)) {
        theEvent.returnValue = false;
        if (theEvent.preventDefault) theEvent.preventDefault();
    }
}

$('#frequencySetting').change(function () {
    let v = parseInt(this.value);
    if (v < 40) this.value = 40;
    if (v > 39000) this.value = 39000;
    pwmFrequencySetting = this.value;
});

$('#dutySetting').change(function () {
    if (this.value < 0) this.value = 0.0;
    if (this.value > 100) this.value = 100.0;
    $('#dutyRangeSetting').val(this.value);
    pwmDutySetting = this.value;
});

$('#dutyRangeSetting').on('input', function () {
    $('#dutySetting').val(this.value)
    pwmDutySetting = this.value;
});


function updateSettings() {
    let buffer = new ArrayBuffer(9);
    let view = new Uint32Array(buffer, 0, 1);
    view[0] = pwmFrequencySetting;
    view = new Float32Array(buffer, 4, 1);
    view[0] = pwmDutySetting;
    view = new Uint8Array(buffer, 8, 1);
    view[0] = autoUpdate ? 0x00 : 0x01;

    mySettingCharacteristic.writeValue(buffer).catch(error => {
        // log('Error: ' + error);
    });
};


