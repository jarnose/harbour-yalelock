import QtQuick 2.0
import Sailfish.Silica 1.0

CoverBackground {
    id: cover

    property var _model: AppController.lockModel
    property bool hasLocks: false
    property var _first: ({})

    // _model.get(0) is a plain method call, not a bound property, so QML's
    // automatic dependency tracking never notices when the underlying row
    // changes (lockStatusChanged, busy, ...) - re-poll it instead, the same
    // way harbour-otpcove's cover keeps its TOTP code current.
    function refresh() {
        hasLocks = _model && _model.count > 0
        _first = hasLocks ? _model.get(0) : ({})
    }

    Timer {
        interval: 1000
        running: true
        repeat: true
        triggeredOnStart: true
        onTriggered: cover.refresh()
    }

    Label {
        anchors {
            top: parent.top
            topMargin: Theme.paddingLarge
            left: parent.left
            right: parent.right
            leftMargin: Theme.paddingMedium
            rightMargin: Theme.paddingMedium
        }
        text: "Yale Lock"
        color: Theme.secondaryColor
        font.pixelSize: Theme.fontSizeSmall
        horizontalAlignment: Text.AlignHCenter
        truncationMode: TruncationMode.Fade
    }

    Column {
        anchors.centerIn: parent
        spacing: Theme.paddingSmall
        visible: hasLocks

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: hasLocks ? _first.name : ""
            color: Theme.secondaryColor
            font.pixelSize: Theme.fontSizeExtraSmall
            truncationMode: TruncationMode.Fade
        }

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: hasLocks && _first.lockStatus ? _first.lockStatus : qsTr("unknown")
            color: _first.lockStatus === "locked" ? Theme.primaryColor : Theme.highlightColor
            font.pixelSize: Theme.fontSizeLarge
            font.family: Theme.fontFamilyHeading
        }
    }

    Label {
        anchors.centerIn: parent
        visible: !hasLocks
        text: qsTr("No locks added")
        color: Theme.secondaryColor
        font.pixelSize: Theme.fontSizeMedium
    }

    CoverActionList {
        enabled: hasLocks && !_first.busy
        CoverAction {
            // No distinct "unlocked" glyph ships in the theme; the one
            // padlock icon toggling what it does (lock vs. unlock,
            // decided below) reads fine without needing a second icon.
            iconSource: "image://theme/icon-m-device-lock"
            onTriggered: _first.lockStatus === "locked"
                         ? AppController.unlockDoor(_first.id)
                         : AppController.lockDoor(_first.id)
        }
    }
}
