`timescale 1ns / 1ps


module pulse_conver #(parameter width = 8,filt_value = 8'hff,default_value = 0)(
    output reg pulse_out,
    input pulse_in,
    input clk,
    input rst
    );
    
    reg[width-1:0] pulse_shift;
    
    always@(posedge clk or negedge rst)
    begin
        if(!rst)
            pulse_shift <= default_value;
        else
            pulse_shift <= {pulse_shift[width-2:0],pulse_in};
    end
    
    always@(*)
    begin
        if(!rst)
            pulse_out = 0;
        else if(pulse_shift == filt_value)
            pulse_out = 1;
        else
            pulse_out = 0;
    end
    
endmodule
