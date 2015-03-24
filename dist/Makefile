include config.mk

.PHONY: all
all: dist$(OBJ_EXT)

dist$(OBJ_EXT): $(DIST_METHOD)$(CXX_EXT)
	@$(CXX) $(CXXFLAGS) $(CXX_OUT_FLAG) $< $(LINK_OUT_FLAG) dist$(OBJ_EXT)
	@echo distribution method: $(DIST_METHOD)
	@echo compiled $<

.PHONY: clean
clean: 
	@rm -f *$(OBJ_EXT) *~
	@echo clean complete
