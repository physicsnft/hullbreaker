file(READ ${COMMON} A)
file(READ ${SRC} B)
file(WRITE ${OUT} "${A}\n${B}")
