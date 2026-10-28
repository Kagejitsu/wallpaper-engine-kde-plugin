import QtQuick
import QtTest
import "../../plugin/contents/ui" as Wallpaper

TestCase {
    name: "DesktopGeometry"

    function test_bounds_data() {
        return [
            { tag: "empty", screens: [], expected: Qt.rect(0, 0, 0, 0) },
            { tag: "single", screens: [
                { virtualX: 0, virtualY: 0, width: 1920, height: 1080 }
            ], expected: Qt.rect(0, 0, 1920, 1080) },
            { tag: "left-and-above", screens: [
                { virtualX: -1920, virtualY: -200, width: 1920, height: 1080 },
                { virtualX: 0, virtualY: 0, width: 2560, height: 1440 }
            ], expected: Qt.rect(-1920, -200, 4480, 1640) },
            { tag: "vertical", screens: [
                { virtualX: 0, virtualY: -1080, width: 1920, height: 1080 },
                { virtualX: 0, virtualY: 0, width: 1920, height: 1080 }
            ], expected: Qt.rect(0, -1080, 1920, 2160) },
            { tag: "mixed-dpr-logical-sizes", screens: [
                { virtualX: 0, virtualY: 0, width: 1920, height: 1080, devicePixelRatio: 1 },
                { virtualX: 1920, virtualY: 0, width: 1920, height: 1080, devicePixelRatio: 2 }
            ], expected: Qt.rect(0, 0, 3840, 1080) },
            { tag: "mirrored", screens: [
                { virtualX: 0, virtualY: 0, width: 1920, height: 1080 },
                { virtualX: 0, virtualY: 0, width: 1920, height: 1080 }
            ], expected: Qt.rect(0, 0, 1920, 1080) }
        ];
    }

    function test_bounds(data) {
        compare(Wallpaper.Common.desktopGeometry(data.screens), data.expected);
    }

    function test_geometryChanges() {
        const screen = Qt.createQmlObject('import QtQuick; QtObject {'
            + 'property int virtualX: 0; property int virtualY: 0;'
            + 'property int width: 1920; property int height: 1080; }', this);
        const geometry = Qt.createQmlObject('import QtQuick; QtObject {'
            + 'property var screens; property var common;'
            + 'property rect desktop: common ? common.desktopGeometry(screens) : Qt.rect(0,0,0,0) }', this);
        geometry.screens = [screen];
        geometry.common = Wallpaper.Common;
        compare(geometry.desktop, Qt.rect(0, 0, 1920, 1080));
        screen.virtualX = -1920;
        screen.height = 1200;
        compare(geometry.desktop, Qt.rect(-1920, 0, 1920, 1200));
        geometry.screens = [];
        compare(geometry.desktop, Qt.rect(0, 0, 0, 0));
        geometry.destroy();
        screen.destroy();
    }
}
