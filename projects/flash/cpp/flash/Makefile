BDBMPCIEDIR=../../../../bluespecpcie/
BDBMPCIEFILES=$(BDBMPCIEDIR)/src/ShmFifo.cpp $(BDBMPCIEDIR)/cpp/bdbmpcie.cpp $(BDBMPCIEDIR)/cpp/dmasplitter.cpp flashmanager.cpp bsbfs.cpp proteinbench/proteinbench.cpp proteinbench/smithwaterman.cpp sparsebench/sparsebench.cpp
BDBMPCIEINCLUDE= -I $(BDBMPCIEDIR)/src/ -I $(BDBMPCIEDIR)/cpp/
LIB= -lrt -lpthread 
FLAGS=


all:
	echo "building for pcie"
	mkdir -p obj
	g++ main.cpp $(BDBMPCIEFILES) $(BDBMPCIEINCLUDE) $(FLAGS) -o obj/main $(LIB) -g -pedantic 
bsim:
	echo "building for bluesim"
	mkdir -p obj
	g++ main.cpp $(BDBMPCIEFILES) $(BDBMPCIEINCLUDE) -o obj/bsim $(LIB) -DBLUESIM -g -pedantic
