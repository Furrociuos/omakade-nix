.pragma library

function format(value, precision) {
    const date = value instanceof Date ? value : new Date(value)
    if (isNaN(date.getTime())) return ""
    const locale = Qt.locale()
    if (precision === "month") return locale.toString(date, "MMMM yyyy")
    if (precision === "year") return locale.toString(date, "yyyy")
    if (precision === "datetime")
        return locale.toString(date, "MMM d, yyyy") + "  ·  " + locale.toString(date, "h:mm AP")
    return locale.toString(date, "MMM d, yyyy")
}
