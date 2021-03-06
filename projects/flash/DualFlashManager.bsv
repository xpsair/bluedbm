import FIFO::*;
import FIFOF::*;
import Clocks::*;
import Vector::*;

import BRAM::*;
import BRAMFIFO::*;

import AuroraImportFmc1::*;
import ControllerTypes::*;
import FlashCtrlVirtex1::*;
import FlashCtrlModel::*;

import MergeN::*;
/**
IMPORTANT: tags need to be encoded in a certain way now:
tag[2:0] tag
tag[5:3] bus
tag[6] board
**/

interface FlashDataIfc;
	method Action writeWord(Bit#(8) tag, Bit#(128) word);
	method ActionValue#(Tuple2#(Bit#(8), Bit#(128))) readWord;
endinterface

typedef enum {
	STATE_NULL,
	STATE_WRITE_READY,
	STATE_WRITE_DONE,
	STATE_ERASE_DONE,
	STATE_ERASE_FAIL
} FlashStatus deriving (Bits, Eq);

typedef struct {
	FlashOp op;
	
	Bit#(8) tag;

	Bit#(4) bus;
	ChipT chip;
	Bit#(16) block;
	Bit#(8) page;
} FlashManagerCmd deriving (Bits, Eq);

interface DualFlashManagerIfc;
	method Action command(FlashManagerCmd cmd);
	method ActionValue#(Tuple2#(Bit#(8), FlashStatus)) fevent;
	interface Vector#(2, FlashDataIfc) ifc;
endinterface

module mkDualFlashManager#(Vector#(2,FlashCtrlUser) flashes) (DualFlashManagerIfc);

	Merge2Ifc#(Tuple2#(Bit#(8), FlashStatus)) mstat <- mkMerge2;
	for (Integer i = 0; i < 2; i=i+1 ) begin
		(* descending_urgency = "flashAck, writeReady" *)
		rule flashAck;
			let ackStatus <- flashes[i].ackStatus();
			Bit#(8) tag = zeroExtend(tpl_1(ackStatus));
			StatusT status = tpl_2(ackStatus);
			FlashStatus stat = STATE_NULL;
			case (status) 
				WRITE_DONE: stat = STATE_WRITE_DONE;
				ERASE_DONE: stat = STATE_ERASE_DONE;
				ERASE_ERROR: stat = STATE_ERASE_FAIL;
			endcase
			Bit#(8) mask = (fromInteger(i)<<7);
			mstat.enq[i].enq(tuple2(tag|mask, stat));
		endrule
		rule writeReady;
			TagT tag <- flashes[i].writeDataReq;
			Bit#(8) mask = (fromInteger(i)<<7);
			Bit#(8) ntag = zeroExtend(tag)|mask;
			mstat.enq[i].enq(tuple2(ntag, STATE_WRITE_READY));
		endrule
	end

	Vector#(2, FlashDataIfc) ifc_;
	for ( Integer i = 0; i < 2; i=i+1 ) begin
		ifc_[i] = interface FlashDataIfc;
			method Action writeWord(Bit#(8) tag, Bit#(128) word);
				flashes[i].writeWord(tuple2(word, truncate(tag)));
			endmethod
			method ActionValue#(Tuple2#(Bit#(8), Bit#(128))) readWord;
				let d <- flashes[i].readWord;
				Bit#(8) mask = (fromInteger(i)<<7);
				return tuple2(zeroExtend(tpl_2(d))|mask, tpl_1(d)) ;
			endmethod
		endinterface: FlashDataIfc;
	end

	method Action command(FlashManagerCmd cmd);
		Bit#(3) bus = truncate(cmd.bus);
		Bit#(7) tag = truncate(cmd.tag);
		if ( cmd.bus[3] == 0 ) begin // board(1), bus(3)
			flashes[0].sendCmd(FlashCmd{
				op:cmd.op,
				tag: tag,
				bus: bus,
				chip: cmd.chip,
				block:cmd.block,
				page:cmd.page
				});
		end else begin
			flashes[1].sendCmd(FlashCmd{
				op:cmd.op,
				tag: tag,
				bus: bus,
				chip: cmd.chip,
				block:cmd.block,
				page:cmd.page
				});
		end
	endmethod
	method ActionValue#(Tuple2#(Bit#(8), FlashStatus)) fevent;
		mstat.deq;
		return mstat.first;
	endmethod
	interface ifc = ifc_;
endmodule
