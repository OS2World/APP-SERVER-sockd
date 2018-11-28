#
# Makefile for SOCKS daemon
#
# Bob Eager   October 1997
#
# Product name
#
PRODUCT		= sockd
#
# Target directory
#
TARGET		= f:\binp
#
# Compiler setup
#
CC		= icc
!IFDEF DEBUG
DBUG		= -DDEBUG
!ELSE
DBUG		=
!ENDIF
!IFDEF	PROD
CFLAGS		= $(DBUG) -Fi -G4 -Gm -Gn -O -Q -Se -Si
!ELSE
CFLAGS		= $(DBUG) -Fi -G4 -Gm -Gn -Q -Se -Si -Ti -Tm -Tx
!ENDIF
#
# Names of library files
#
NETLIB = 	..\netlib\netlib.lib
LIBS =		so32dll.lib tcp32dll.lib cppom30o.lib \
		$(NETLIB) os2386.lib
#
# Names of object files
#
OBJ =		sockd.obj config.obj server.obj log.obj
#
# Other files
#
DEF =		$(PRODUCT).def
LNK =		$(PRODUCT).lnk
#
# Final executable file
#
EXE =		$(PRODUCT).exe
#
#-----------------------------------------------------------------------------
#
install:	$(EXE)
		@copy $(EXE) $(TARGET) > nul
#
$(EXE):		$(OBJ) $(NETLIB) $(LNK) $(DEF)
!IFDEF	PROD
		ilink /nodefaultlibrarysearch /nologo /exepack:2 @$(LNK)
!ELSE
		ilink /nodefaultlibrarysearch /debug /nobrowse /nologo @$(LNK)
!ENDIF
#
# Object files
#
sockd.obj:	sockd.c sockd.h log.h
#
config.obj:	config.c sockd.h log.h cmds.h
#
server.obj:	server.c sockd.h log.h
#
log.obj:	log.c log.h
#
# Linker response file. Rebuild if makefile changes
#
$(LNK):		makefile
		@if exist $(LNK) erase $(LNK)
		@echo /map:$(PRODUCT) >> $(LNK)
		@echo /out:$(PRODUCT) >> $(LNK)
		@echo $(OBJ) >> $(LNK)
		@echo $(LIBS) >> $(LNK)
		@echo $(DEF) >> $(LNK)
#
clean:		
		-erase $(OBJ) $(LNK) $(PRODUCT).map csetc.pch
#
# End of makefile for SOCKS daemon
#

