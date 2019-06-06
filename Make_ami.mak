#------------------------------------------------------------------------------------------
# Naming and paths
#------------------------------------------------------------------------------------------
MKF:=Make_ami.mak
DST:=dist
SRC:=src

#------------------------------------------------------------------------------------------
# Build Vim
#------------------------------------------------------------------------------------------
.PHONY: vim
vim: $(SRC)/.ver $(SRC)/.pat
	$(MAKE) -C $(SRC) -f $(MKF) PATCHLEVEL=`cat $(SRC)/.pat`

#------------------------------------------------------------------------------------------
# Determine version
#------------------------------------------------------------------------------------------
$(SRC)/.ver: $(SRC)/version.h
	grep -E "#.\{1,\}_SHORT" $< | sed -e "s/.*\"\([0-9]\.[0-9]\)\".*/\1/" > $@

#------------------------------------------------------------------------------------------
# Determine patch number
#------------------------------------------------------------------------------------------
$(SRC)/.pat: $(SRC)/version.c
	grep -E -m1 "^ \{4\}[0-9]\{1,4\},$$" $< | tr -d "[:space:]," > $@

#------------------------------------------------------------------------------------------
# Create archive
#------------------------------------------------------------------------------------------
.PHONY: $(DST)
$(DST): $(SRC)/vim
	$(MAKE) -C $(DST)

#------------------------------------------------------------------------------------------
# Phony standard targets
#------------------------------------------------------------------------------------------
.PHONY: clean
clean:
	$(MAKE) -C $(DST) $@
	$(MAKE) -C $(SRC) -f $(MKF) $@
	rm -f $(SRC)/.pat $(SRC)/.ver
