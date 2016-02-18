#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>


#include "bsbfs.h"


File::File(std::string name) {
	filename = name;
	size = 0;
	blockmap.clear();
	appendbuffer = (uint8_t*)malloc(FPAGE_SIZE);
	readbuffer = (uint8_t*)malloc(FPAGE_SIZE);
}
void
File::clear() {
	blockmap.clear();
	free(appendbuffer);
	free(readbuffer);
}

/// File stuff end

BSBFS*
BSBFS::m_pInstance = NULL;

BSBFS*
BSBFS::getInstance() {
	if ( m_pInstance == NULL ) m_pInstance = new BSBFS();

	return m_pInstance;
}

BSBFS::BSBFS() {
	//create eraser thread

	// TODO load?
	cur_blockeraseidx = 0;
	pthread_mutex_init(&eraseMutex, NULL);
	pthread_cond_init(&eraseCond, NULL);

	pthread_create(&eraserThread, NULL, blockEraserThread, NULL);
}
	
int 
BSBFS::createFile(std::string name) {
	//TODO lock?
	int i;
	//std::string name = std::string(filename);
	for ( i = 0; i < files.size(); i++ ) {
		if ( files[i] != NULL
			&& files[i]->filename == name ) break;
	}
	if ( i < files.size() ) {
		return -File::EEXIST;
	}
	
	int fd = -1;
	File* nf = new File(name);
	for ( i = 0; i < files.size(); i++ ) {
		if ( files[i] == NULL ) {
			fd = i;
			break;
		}
	}

	if (fd >= 0 ) {
		files[fd] = nf;
	} else {
		files.push_back(nf);
		fd = files.size() - 1;
	}

	return fd;
}

int 
BSBFS::deleteFile(std::string name) {
	int i;
	//std::string name = std::string(filename);
	for ( i = 0; i < files.size(); i++ ) {
		if ( files[i] != NULL
			&& files[i]->filename == name ) break;
	}
	if ( i >= files.size() ) {
		return -File::ENOENT;
	}

	File* nf = files[i];
	nf->clear();
	delete ( nf );
	files[i] = NULL;

	return 0;
}
	
int 
BSBFS::open(std::string filename) {
	int i;
	std::string name = std::string(filename);
	for ( i = 0; i < files.size(); i++ ) {
		if ( files[i] != NULL
			&& files[i]->filename == name ) break;
	}
	if ( i >= files.size() ) {
		return -File::ENOENT;
	}

	return i;
}


int 
BSBFS::fseek(int fd, uint64_t offset, int whence) {
	File* nf = files[fd];
	if ( nf == NULL ) return -File::ENOENT;

	if ( whence == File::FSEEK_SET ) {
		nf->seek = offset;
		return 0;
	} else if ( whence == File::FSEEK_CUR) {
		nf->seek += offset;
		return 0;
	} else {
		return -File::EINVAL;
	}
}

uint64_t 
BSBFS::ftell(int fd) {
	File* nf = files[fd];
	if ( nf == NULL ) return -1;

	return nf->seek;
}


/*
// page/block map: [addr. bits]
	block[remain]:chip[1]:page[8]:chip[2]:bus[3]

	BSIM:
	block[remain]:chip[1]:page[4]:chip[2]:bus[3]
*/
void
BSBFS::pageMap(uint64_t page, PhysPage& np) {
#ifdef BLUESIM
	int bus = page & 0x7;
	int chipd = (page>>3) & 0x3;
	int page_ = (page>>5) & 0xf;
	int chipu = (page>>9) & 0x1;
	int block = (page>>10);
#else
	int bus = page & 0x7;
	int chipd = (page>>3) & 0x3;
	int page_ = (page>>5) & 0xff;
	int chipu = (page>>13) & 0x1;
	int block = (page>>14);
#endif
	
	np.bus = bus;
	np.chip = ((chipu<<2) | chipd);
	np.block = block;
	np.page = page_;
}
uint32_t
BSBFS::blockIdx(uint64_t page) {
	uint64_t down = page & 0x1f;
#ifdef BLUESIM
	uint64_t up = (page>>9);
#else
	uint64_t up = (page>>13);
#endif
	uint64_t idx = down | (up<<5);

	return (uint32_t)idx;
}

int 
BSBFS::fread(void* ptr, uint64_t size, int fd) {
}

int 
BSBFS::fappend(int fd, void* buffer, uint64_t size) {
	FlashManager* flash = FlashManager::getInstance();

	uint8_t* statusbuffer = (uint8_t*)malloc(size/FPAGE_SIZE+1);
	memset(statusbuffer, 0, size/FPAGE_SIZE+1);
	int wcount = 0;

	File* nf = files[fd];
	if ( nf == NULL ) return -1;

	uint8_t* buf = (uint8_t*)buffer;

	uint64_t ioff = size & 0x1fff;
	if ( size + ioff < FPAGE_SIZE ) {
		memcpy((nf->appendbuffer)+ioff, buffer, size);
		nf->size += size;
		return 0;
	}

	//size + ioff >= FPAGE_SIZE from here

	size_t ileft = FPAGE_SIZE - ioff;

	memcpy((nf->appendbuffer)+ioff, buffer, ileft);
	
	uint64_t pageoff = ((nf->size)>>13);

	uint32_t bidx = BSBFS::blockIdx(pageoff);
	while ( nf->blockmap.size() <= bidx ) {
		pthread_mutex_lock(&eraseMutex);
		while ( listErased.empty() ) {
			pthread_cond_wait(&eraseCond, &eraseMutex);
		}

		uint32_t bc = listErased.front();
		listErased.pop_front();
		nf->blockmap.push_back(bc);
		pthread_mutex_unlock(&eraseMutex);
	}

	uint32_t bl = nf->blockmap[bidx];
	int bus = bl & 0x7;
	int chip = (bl>>3) & 0x7;
	int block = bl>>6;

	PhysPage mapped;
	BSBFS::pageMap(pageoff, mapped);
	flash->writePage(bus,chip,block,mapped.page, nf->appendbuffer, &statusbuffer[wcount++]);
	
	size -= ileft;
	nf->size += ileft;
	buf += ileft;

	while ( size > 0 ) {
		if ( size < FPAGE_SIZE ) {
			memcpy(nf->appendbuffer, buf, size);

			nf->size += size;
			size = 0;
			break;
		} 

		uint64_t pageoff = ((nf->size)>>13);

		uint32_t bidx = BSBFS::blockIdx(pageoff);
		while ( nf->blockmap.size() <= bidx ) {
			pthread_mutex_lock(&eraseMutex);
			while ( listErased.empty() ) {
				pthread_cond_wait(&eraseCond, &eraseMutex);
			}

			uint32_t bc = listErased.front();
			listErased.pop_front();
			nf->blockmap.push_back(bc);
			pthread_mutex_unlock(&eraseMutex);
		}

		uint32_t bl = nf->blockmap[bidx];
		int bus = bl & 0x7;
		int chip = (bl>>3) & 0x7;
		int block = bl>>6;

		PhysPage mapped;
		BSBFS::pageMap(pageoff, mapped);
		flash->writePage(bus,chip,block,mapped.page, buf, &statusbuffer[wcount++]); 
		
		size -= FPAGE_SIZE;
		nf->size += FPAGE_SIZE;
		buf += ileft;
	}

	//TODO wait until writes are done
	free(statusbuffer);
}


void
BSBFS::fileList() {
	int fcount = 0;
	for ( int i = 0; i < files.size(); i++ ) {
		if ( files[i] == NULL ) continue; 
		fcount++;

		printf( "%s : %ld\n", files[i]->filename.c_str(), files[i]->size );
	}
	/*
	File::Stat* stats = (File::Stat*)malloc(sizeof(File::Stat)*fcount);
	for ( int i = 0; i < fcount; i++ ) {
		stats[i]->name = files[i].name;
		stats[i]->size = files[i].size;
	}
	*/
}

void* blockEraserThread(void* arg) {
	BSBFS* fs = BSBFS::getInstance();
	FlashManager* flash = FlashManager::getInstance();

	uint8_t erase_reqstate[BSBFS_ERASE_INFLIGHT];
	uint32_t erase_reqaddr[BSBFS_ERASE_INFLIGHT];
	int erase_inflight = 0;
	while (1) {
		pthread_mutex_lock(&fs->eraseMutex);
		size_t les = fs->listErased.size();
		pthread_mutex_unlock(&fs->eraseMutex);

		while ( les >= BSBFS_ERASE_PREPARE ) {
			usleep(10000);
			pthread_mutex_lock(&fs->eraseMutex);
			les = fs->listErased.size();
			pthread_mutex_unlock(&fs->eraseMutex);
		}

		// Need to fill erased list

		while ( les < BSBFS_ERASE_PREPARE ) {
			for ( int i = 0; i < BSBFS_ERASE_INFLIGHT; i++ ) {
				// Erase success
				if ( erase_reqstate[i] == FLASHSTAT_ERASE_DONE ) {

					pthread_mutex_lock(&fs->eraseMutex);
					fs->listErased.push_back(erase_reqaddr[i]);
					les = fs->listErased.size();
					pthread_cond_broadcast(&fs->eraseCond);
					pthread_mutex_unlock(&fs->eraseMutex);
					erase_reqstate[i] = 0;
					erase_inflight--;
				} 
				// Erase failed
				if ( erase_reqstate[i] == FLASHSTAT_ERASE_FAIL ) {
					erase_reqstate[i] = 0;
					erase_inflight--;
				}
			}

			int eraseslot = -1;
			for ( int i = 0; i < BSBFS_ERASE_INFLIGHT; i++ ) {
				if ( erase_reqstate[i] == 0 ) {
					eraseslot = i;
					break;
				}
			}
			if ( eraseslot < 0 ) continue;
			uint8_t* slot = &(erase_reqstate[eraseslot]);
			*slot = FLASHSTAT_ERASE_WAIT;

			uint32_t curblock = fs->cur_blockeraseidx;
			int bus = curblock & 0x07;
			int chip = (curblock>>3) & 0x07;
			int block = (curblock>>6);
			//printf( "%d --E\n", curblock ); fflush(stdout);

			flash->eraseBlock(bus,chip,block, slot);
			erase_inflight++;

			erase_reqaddr[eraseslot] = curblock;
			(fs->cur_blockeraseidx)++;
		}
	}
}