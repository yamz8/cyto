import QtQuick

// The toolbar's small line icons, drawn so they follow the theme's colors.
Canvas {
    id: icon
    property string kind
    property color color: "#d6d6dc"
    implicitWidth: 18
    implicitHeight: 18
    onColorChanged: requestPaint()
    onKindChanged: requestPaint()

    onPaint: {
        const c = getContext("2d")
        c.reset()
        c.strokeStyle = color
        c.fillStyle = color
        c.lineWidth = 1.6
        c.lineJoin = "round"
        c.beginPath()
        switch (kind) {
        case "select":
            c.moveTo(5, 2); c.lineTo(5, 15); c.lineTo(8.3, 11.8); c.lineTo(10.6, 16.6)
            c.lineTo(12.6, 15.7); c.lineTo(10.4, 11); c.lineTo(14.6, 11); c.closePath(); c.fill()
            break
        case "rectangle":
            c.rect(2.5, 4, 13, 10); c.stroke()
            break
        case "polygon":
            c.moveTo(9, 2.5); c.lineTo(15.5, 7.2); c.lineTo(12.8, 15.2); c.lineTo(5.2, 15.2); c.lineTo(2.5, 7.2)
            c.closePath(); c.stroke()
            break
        case "ellipse":
            c.ellipse(2.2, 4, 13.6, 10); c.stroke()
            break
        case "dots":
            for (const p of [[4, 13], [6, 10], [8, 12], [9, 7], [11, 9], [12, 5], [14, 7], [7, 15], [13, 12]]) {
                c.moveTo(p[0] + 1.3, p[1]); c.arc(p[0], p[1], 1.3, 0, 2 * Math.PI)
            }
            c.fill()
            break
        case "histogram":
            for (const b of [[2, 12, 3], [5.5, 7, 3], [9, 4, 3], [12.5, 9, 3]])
                c.rect(b[0], b[1], b[2], 16 - b[1])
            c.fill()
            break
        case "swap":
            c.moveTo(3, 6); c.lineTo(15, 6); c.moveTo(11.5, 2.5); c.lineTo(15, 6); c.lineTo(11.5, 9.5)
            c.moveTo(15, 12); c.lineTo(3, 12); c.moveTo(6.5, 8.5); c.lineTo(3, 12); c.lineTo(6.5, 15.5)
            c.stroke()
            break
        }
    }
}
