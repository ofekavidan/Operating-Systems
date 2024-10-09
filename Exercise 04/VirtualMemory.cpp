#include "VirtualMemory.h"
#include "PhysicalMemory.h"

// a struct containing data that is passed while recursively calling evictPage
struct RecursiveEvictVariables {
    word_t curFrame;
    uint64_t p;
    word_t parentFrame;
    uint64_t parentEntry;
    bool withRecursive;
    uint64_t curDepth;
    word_t pageSwappedIn;
} typedef RecursiveEvictVariables;


/**
 * a function that calculates the absolute of a number
 * @param num the number
 * @return the absolute of a number
 */
word_t absFunc(word_t num) {
    if (num < 0) {
        return -num;
    }
    return num;
}

/**
 * a function that calculates the minimum between two numbers
 * @param first first number
 * @param second second number
 * @return min(first,second)
 */
word_t minFunc(word_t first, word_t second) {
    if (first < second) {
        return first;
    }
    return second;
}

/**
 * This function finds a page to evict by traversing the RAM recursively
 * @param maxVal current maximum value of cyclical distance calculated by:
 *        min{NUM_PAGES - |page_swapped_in - p|, |page_swapped_in - p|}
 * @param frame - the frame to evict
 * @param page - the page
 * @param parentEvicted - parent of evicted frame
 * @param parentEntryEvicted - the entry inside the parent frame
 * @param REV REV data
 */
void EvictPage(uint64_t &maxVal, word_t &frame, uint64_t &page, word_t &parentEvicted,
               uint64_t &parentEntryEvicted, RecursiveEvictVariables REV) {
    if (REV.withRecursive) {
        REV.p = ((REV.p << OFFSET_WIDTH) | REV.parentEntry);
    }
    //if we in a leaf:
    if (REV.curDepth == TABLES_DEPTH) {
        uint64_t curVal = minFunc(
                NUM_PAGES - absFunc(REV.pageSwappedIn - REV.p), absFunc(REV.pageSwappedIn - REV.p));
        //check if it's the maximum
        if (curVal > maxVal) {
            maxVal = curVal;
            frame = REV.curFrame;
            page = REV.p;
            parentEvicted = REV.parentFrame;
            parentEntryEvicted = REV.parentEntry;
        }
        return;
    }
    word_t temp;
    for (int i = 0; i < PAGE_SIZE; i++) {
        PMread(REV.curFrame * PAGE_SIZE + i, &temp);
        if (temp != 0) {
            RecursiveEvictVariables tempREV = REV;
            REV.withRecursive = true;
            REV.curDepth = REV.curDepth + 1;
            REV.parentFrame = REV.curFrame;
            REV.parentEntry = i;
            REV.curFrame = temp;

            EvictPage(maxVal, frame, page, parentEvicted, parentEntryEvicted, REV);
            REV = tempREV;
        }
    }
}

/**
 * this function checks a frame entry value and returns it
 * @param frame the  frame to check
 * @param entry the entry of the frame
 * @return the value of the frame
 */
word_t checkFrameEntryVal(word_t frame, int entry) {
    word_t value;
    PMread(frame * PAGE_SIZE + entry, &value);
    return value;
}

/**
 * this function finds an empty frame in the RAM by traversing the RAM recursively.
 * @param curDepth current depth
 * @param curFrame current frame
 * @param maxFrameNum maximum frame index that exists in the RAM (and is used)
 * @param savedFrame a frame that we cant use
 * @param RAMAddress the RAM address of the current frame
 * @return a frame that all her entries are zeros.
 */
word_t findEmptyFrame(uint64_t curDepth, word_t curFrame, word_t &maxFrameNum, word_t savedFrame, word_t RAMAddress) {
    if (curDepth == TABLES_DEPTH) {
        return -1;
    }

    int isEmpty = 1;
    word_t nextFrame;
    //iterate over all the frame entries and check if all of them are zeros, if not, go recursively to the value and
    // check its frame.
    for (int i = 0; i < PAGE_SIZE; i++) {
        word_t entryVal = checkFrameEntryVal(curFrame, i);
        if (entryVal != 0) {
            isEmpty = 0;
            if (maxFrameNum < entryVal) {
                maxFrameNum = entryVal;
            }
            nextFrame = findEmptyFrame(curDepth + 1, entryVal, maxFrameNum, savedFrame, (curFrame * PAGE_SIZE + i));
            if (nextFrame != -1) {
                return nextFrame;
            }
        }
    }
    //if all are empty and cur frame not saved initialize it and return
    if (isEmpty == 1 && curFrame != savedFrame) {
        PMwrite(RAMAddress, 0);
        return curFrame;
    }
    return -1;
}


/**
 * this function finds a new frame in the RAM
 * @param savedFrame frame that shouldn't be evicted later
 * @param pageSwappedIn wanted page
 * @return new available frame
 */
word_t findNewFrame(word_t savedFrame, word_t pageSwappedIn) {
    //initialize a variable with the maximal frame Index referenced from any table we visit.
    word_t maxFrameIndex = 0;

    //three conditions:
    //first- A frame containing an empty table – where all rows are 0.
    word_t newFrame = findEmptyFrame(0, 0, maxFrameIndex, savedFrame, 0);
    if (newFrame != -1) {
        return newFrame;
    }
    //second - An unused frame – When traversing the tree, we keep a variable with the maximal frame Index referenced
    // from any table we visit- maxFrameIndex. Since we fill frames in order, all used frames are contiguous in
    //the memory, and if maxFrameIndex+1 < NUM_FRAMES then we know that the frame in the index (maxFrameIndex + 1)
    // is unused.
    if (maxFrameIndex + 1 < NUM_FRAMES) {
        return maxFrameIndex + 1;
    }

    // third - If all frames are already used, then a page must be swapped out from some frame in order to replace
    // it with the relevant page (a table or actual page). The frame that will be chosen is the frame containing a page
    // p such that the cyclical distance: min{NUM_PAGES - |page_swapped_in - p|, |page_swapped_in - p|} is maximal.
    // This page must be swapped out before we use the frame, and we also  have to remove the reference to this page
    // from its parent table.

    RecursiveEvictVariables REV = {0, 0, 0, 0, false, 0, pageSwappedIn};

    uint64_t maxVal = 0;
    word_t frame = 0;
    uint64_t page = 0;
    word_t evictedParentFrame = 0;
    uint64_t evictedParentOffset = 0;

    EvictPage(maxVal, frame, page, evictedParentFrame, evictedParentOffset, REV);
    PMevict(frame, page);
    PMwrite(evictedParentFrame * PAGE_SIZE + evictedParentOffset, 0);
    return frame;
}

/**
 * this function gets a virtual address and translates it to a physical address
 * @param virtual_address the virtual address
 * @return the physical address
 */
uint64_t translateVirtualToPhysical(uint64_t virtual_address) {
    uint64_t offset = virtual_address & ((1ll << OFFSET_WIDTH) - 1);
    uint64_t wantedPage = virtual_address >> OFFSET_WIDTH;
    word_t curFrame = 0;
    word_t value = 0;
    bool restoreFlag = false;
    for (uint64_t i = TABLES_DEPTH; i > 0; i--) {
        //split to chunks
        uint64_t curChunk =
                (wantedPage >> ((i - 1) * OFFSET_WIDTH)) & ((1ll << OFFSET_WIDTH) - 1);
        //try to read
        PMread(curFrame * PAGE_SIZE + curChunk, &value);
        //check if there is something in the physical address
        if (value == 0) {
            //find new frame
            word_t newFrame = findNewFrame(curFrame, (word_t) wantedPage);
            //initialize the new frame to contain zeros
            for (uint64_t j = 0; j < PAGE_SIZE; j++) {
                PMwrite(newFrame * PAGE_SIZE + j, 0);
            }
            // write to frame
            PMwrite(curFrame * PAGE_SIZE + curChunk, newFrame);
            curFrame = newFrame; //update the cur frame
            restoreFlag = true;
        } else {
            curFrame = value;
        }
    }
    if (restoreFlag) {
        PMrestore(curFrame, wantedPage);
    }
    return curFrame * PAGE_SIZE + offset;
}


void VMinitialize() {
    for (uint64_t i = 0; i < PAGE_SIZE; i++) {
        PMwrite(i, 0);
    }
}


int VMread(uint64_t virtualAddress, word_t *value) {
    if (virtualAddress >= VIRTUAL_MEMORY_SIZE) {
        return 0;
    }
    uint64_t physicalAddress = translateVirtualToPhysical(virtualAddress);
    PMread(physicalAddress, value);
    return 1;
}


int VMwrite(uint64_t virtualAddress, word_t value) {
    if (virtualAddress >= VIRTUAL_MEMORY_SIZE) {
        return 0;
    }
    uint64_t physicalAddress = translateVirtualToPhysical(virtualAddress);
    PMwrite(physicalAddress, value);
    return 1;
}