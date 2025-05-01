

SET(UQMI uqmi.c dev.c commands.c ${SOURCES})

ADD_EXECUTABLE(uqmi ${UQMI})
ADD_DEPENDENCIES(uqmi gen-headers gen-errors)

FIND_PACKAGE(Iconv)

TARGET_LINK_LIBRARIES(uqmi ${LIBS} ${Iconv_LIBRARIES} common qmigen)
TARGET_INCLUDE_DIRECTORIES(uqmi PRIVATE ${ubox_include_dir} ${blobmsg_json_include_dir} ${json_include_dir} ${Iconv_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR})

INSTALL(TARGETS uqmi
	RUNTIME DESTINATION sbin
)
