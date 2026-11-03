import QtQuick

// Stand-in for the native system-audio capture used by the web backend.
QtObject {
    objectName: "fixtureAudioSpectrum"
    property bool active: false
    signal frame(var audioArray)
}
