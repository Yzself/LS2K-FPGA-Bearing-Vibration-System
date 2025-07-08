`timescale 1ns / 1ps


//与cnt模块功能一致，只是换为了下降沿采样，具体功能查阅cnt模块
module cnt_negedge
    #(parameter
    dir = 0,            //方向控制
    en = 0,             //是否打开使能功能，与双向互斥
    limit = 0,          //是否打开限制溢出
    var_option = 0,     //是否打开上限值可变化
    var = var_option,   //
    dual = 0,           //是否打开双向计数功能（打开dual为1时en要设置为0）
    done_reg = 0,       //done输出是否为寄存器形式（除非多个计数器级联导致时序分析通过不了，否则不要打开）
    max_value = 10,     //最大上限值
    width = max_value > 0 ? clog2(max_value) : 1    //自动计算，无需配置
    )
    (
    output reg[width-1:0] cnt_value,    //计数器实时值
    output done,                        //计数器产生上/下溢出
    output inc_done,                    //计数器上溢
    output dec_done,                    //计数器下溢
    output empty,                       //计数器为空标志位
    output reg full,                    //计数器为满标志位
    input[width:0] var_value,           //计数器可配置上限值
    input[width-1:0] load_value,          //计数器装载值
    input load,                         //计数器载入数值控制
    input cnt_en,                       //计数器使能
    input inc,                          //双向计数器加使能
    input dec,                          //双向计数器减使能
    input clk,                          //模块时钟，低电平采样
    input rst                           //高电平有效复位
    );
    
    function integer clog2;
    input integer value;
    begin
        value = value - 1;
        for (clog2 = 0; value > 0; clog2 = clog2 + 1) begin
            value = value >> 1;
        end
    end
    endfunction
    
    wire done_en;
    wire done_dual;
    wire done_inc,done_dec;
    wire[width-1:0] max_condition;
    wire[width:0] var_condition;
    wire[width:0] zero_condition;
    
    reg done_buf,done_inc_buf,done_dec_buf;
    reg done_logic,done_inc_logic,done_dec_logic;
    
    assign done = done_reg ? (en ? done_en : (dual ? done_dual : done_buf )) : done_logic;
    assign inc_done = done_reg ? done_inc : done_inc_logic;
    assign dec_done = done_reg ? done_dec : done_dec_logic;
    assign done_en = done_buf & cnt_en;
    assign done_dual = inc_done | dec_done;
    assign done_inc = done_inc_buf & inc && ~dec;
    assign done_dec = done_dec_buf & ~inc && dec;
    assign empty = cnt_value == 0;
    assign max_condition = done_reg ? ((max_value > 1) ? max_value - 2 : max_value - 1) : max_value - 1;
    assign var_condition = done_reg ? ((var_value > 1) ? var_value - 2 : var_value - 1) : var_value - 1;
    assign zero_condition = done_reg ? ((max_value > 1) ? 1 : 0) : 0;
    
    always@(negedge clk or negedge rst)
    begin
        if(var) begin
            if(dual) begin
                if(limit) begin
                    if(!rst) begin
                        cnt_value <= 0;
                    end
                    else if(load)
                        cnt_value <= load_value;
                    else begin
                        case({inc,dec})
                            2'b00: cnt_value <= cnt_value;
                            2'b01: begin
                                if(cnt_value == 0)
                                    cnt_value <= cnt_value;
                                else
                                    cnt_value <= cnt_value - 1;
                            end
                            2'b10: begin
                                if(cnt_value == var_value - 1)
                                    cnt_value <= cnt_value;
                                else
                                    cnt_value <= cnt_value + 1;
                            end
                            2'b11: cnt_value <= cnt_value;
                        endcase
                    end
                end
                else begin
                    if(!rst) begin
                        cnt_value <= 0;
                    end
                    else if(load)
                        cnt_value <= load_value;
                    else begin
                        case({inc,dec})
                            2'b00: cnt_value <= cnt_value;
                            2'b01: begin
                                if(cnt_value == 0)
                                    cnt_value <= var_value - 1;
                                else
                                    cnt_value <= cnt_value - 1;
                            end
                            2'b10: begin
                                if(cnt_value == var_value - 1)
                                    cnt_value <= 0;
                                else
                                    cnt_value <= cnt_value + 1;
                            end
                            2'b11: cnt_value <= cnt_value;
                        endcase
                    end
                end
            end
            else if(en) begin
                if(!rst) begin
                    if(dir)
                        cnt_value <= max_value - 1;
                    else
                        cnt_value <= 0;
                end
                else if(load)
                    cnt_value <= load_value;
                else if(cnt_en) begin
                    if(limit) begin
                        if(dir) begin
                            if(cnt_value == 0)
                                cnt_value <= cnt_value;
                            else
                                cnt_value <= cnt_value - 1;
                        end
                        else begin
                            if(cnt_value == var_value - 1)
                                cnt_value <= cnt_value;
                            else
                                cnt_value <= cnt_value + 1;
                        end
                    end
                    else begin
                        if(dir) begin
                            if(cnt_value == 0)
                                cnt_value <= var_value - 1;
                            else
                                cnt_value <= cnt_value - 1;
                        end
                        else begin
                            if(cnt_value == var_value - 1)
                                cnt_value <= 0;
                            else
                                cnt_value <= cnt_value + 1;
                        end
                    end
                end
            end
            else begin
                if(!rst) begin
                    if(dir)
                        cnt_value <= max_value - 1;
                    else
                        cnt_value <= 0;
                end
                else if(load)
                    cnt_value <= load_value;
                else begin
                    if(limit) begin
                        if(dir) begin
                            if(cnt_value == 0)
                                cnt_value <= cnt_value;
                            else
                                cnt_value <= cnt_value - 1;
                        end
                        else begin
                            if(cnt_value == var_value - 1)
                                cnt_value <= cnt_value;
                            else
                                cnt_value <= cnt_value + 1;
                        end
                    end
                    else begin
                        if(dir) begin
                            if(cnt_value == 0)
                                cnt_value <= var_value - 1;
                            else
                                cnt_value <= cnt_value - 1;
                        end
                        else begin
                            if(cnt_value == var_value - 1)
                                cnt_value <= 0;
                            else
                                cnt_value <= cnt_value + 1;
                        end
                    end
                end
            end
        end
        else begin
            if(dual) begin
                if(limit) begin
                    if(!rst) begin
                        cnt_value <= 0;
                    end
                    else if(load)
                        cnt_value <= load_value;
                    else begin
                        case({inc,dec})
                            2'b00: cnt_value <= cnt_value;
                            2'b01: begin
                                if(cnt_value == 0)
                                    cnt_value <= cnt_value;
                                else
                                    cnt_value <= cnt_value - 1;
                            end
                            2'b10: begin
                                if(cnt_value == max_value - 1)
                                    cnt_value <= cnt_value;
                                else
                                    cnt_value <= cnt_value + 1;
                            end
                            2'b11: cnt_value <= cnt_value;
                        endcase
                    end
                end
                else begin
                    if(!rst) begin
                        cnt_value <= 0;
                    end
                    else if(load)
                        cnt_value <= load_value;
                    else begin
                        case({inc,dec})
                            2'b00: cnt_value <= cnt_value;
                            2'b01: begin
                                if(cnt_value == 0)
                                    cnt_value <= max_value - 1;
                                else
                                    cnt_value <= cnt_value - 1;
                            end
                            2'b10: begin
                                if(cnt_value == max_value - 1)
                                    cnt_value <= 0;
                                else
                                    cnt_value <= cnt_value + 1;
                            end
                            2'b11: cnt_value <= cnt_value;
                        endcase
                    end
                end
            end
            else if(en) begin
                if(!rst) begin
                    if(dir)
                        cnt_value <= max_value - 1;
                    else
                        cnt_value <= 0;
                end
                else if(load)
                    cnt_value <= load_value;
                else if(cnt_en) begin
                    if(limit) begin
                        if(dir) begin
                            if(cnt_value == 0)
                                cnt_value <= cnt_value;
                            else
                                cnt_value <= cnt_value - 1;
                        end
                        else begin
                            if(cnt_value == max_value - 1)
                                cnt_value <= cnt_value;
                            else
                                cnt_value <= cnt_value + 1;
                        end
                    end
                    else begin
                        if(dir) begin
                            if(cnt_value == 0)
                                cnt_value <= max_value - 1;
                            else
                                cnt_value <= cnt_value - 1;
                        end
                        else begin
                            if(cnt_value == max_value - 1)
                                cnt_value <= 0;
                            else
                                cnt_value <= cnt_value + 1;
                        end
                    end
                end
            end
            else begin
                if(!rst) begin
                    if(dir)
                        cnt_value <= max_value - 1;
                    else
                        cnt_value <= 0;
                end
                else if(load)
                    cnt_value <= load_value;
                else begin
                    if(limit) begin
                        if(dir) begin
                            if(cnt_value == 0)
                                cnt_value <= cnt_value;
                            else
                                cnt_value <= cnt_value - 1;
                        end
                        else begin
                            if(cnt_value == max_value - 1)
                                cnt_value <= cnt_value;
                            else
                                cnt_value <= cnt_value + 1;
                        end
                    end
                    else begin
                        if(dir) begin
                            if(cnt_value == 0)
                                cnt_value <= max_value - 1;
                            else
                                cnt_value <= cnt_value - 1;
                        end
                        else begin
                            if(cnt_value == max_value - 1)
                                cnt_value <= 0;
                            else
                                cnt_value <= cnt_value + 1;
                        end
                    end
                end
            end
        end
    end
    
    always@(negedge clk or negedge rst)
    begin
        if(en == 1 && done_reg == 1) begin
            if(!rst)
                done_buf <= 0;
            else if(load)
                done_buf <= 0;
            else if(done_buf == 1) begin
                if(done)
                    done_buf <= 0;
                else
                    done_buf <= 1;
            end
            else begin
                done_buf <= done_logic;
            end  
        end
        else begin
            if(!rst)
                done_buf <= 0;
            else if(done_buf)
                done_buf <= 0;
            else
                done_buf <= done_logic;
        end
    end
    
    always@(negedge clk or negedge rst)
    begin
        if(!rst)
            done_inc_buf <= 0;
        else if(load)
            done_inc_buf <= 0;
        else if(done_inc_buf) begin
            if(inc_done | (~inc & dec))
                done_inc_buf <= 0;
            else
                done_inc_buf <= 1;
        end
        else
            done_inc_buf <= done_inc_logic;
    end
    
    always@(negedge clk or negedge rst)
    begin
        if(!rst)
            done_dec_buf <= 0;
        else if(load)
            done_dec_buf <= 0;
        else if(done_dec_buf) begin
            if(dec_done | (inc & ~dec))
                done_dec_buf <= 0;
            else
                done_dec_buf <= 1;
        end
        else
            done_dec_buf <= done_dec_logic;
    end
    
    always@(*)
    begin
        done_inc_logic = 0;
        if(var) begin
            if(limit)
                done_inc_logic = 0;
            else if(dual) begin
                case({inc,dec})
                    2'b00: done_inc_logic = 0;
                    2'b01: done_inc_logic = 0;
                    2'b10: done_inc_logic = cnt_value >= var_condition;
                    2'b11: done_inc_logic = 0;
                endcase
            end
        end
        else begin
            if(limit)
                done_inc_logic = 0;
            else if(dual) begin
                case({inc,dec})
                    2'b00: done_inc_logic = 0;
                    2'b01: done_inc_logic = 0;
                    2'b10: done_inc_logic = cnt_value >= max_condition;
                    2'b11: done_inc_logic = 0;
                endcase
            end
        end
    end
    
    always@(*)
    begin
        done_dec_logic = 0;
        if(var) begin
            if(limit)
                done_dec_logic = 0;
            else if(dual) begin
                case({inc,dec})
                    2'b00: done_dec_logic = 0;
                    2'b01: done_dec_logic = cnt_value <= zero_condition;
                    2'b10: done_dec_logic = 0;
                    2'b11: done_dec_logic = 0;
                endcase
            end
        end
        else begin
            if(limit)
                done_dec_logic = 0;
            else if(dual) begin
                case({inc,dec})
                    2'b00: done_dec_logic = 0;
                    2'b01: done_dec_logic = cnt_value <= zero_condition;
                    2'b10: done_dec_logic = 0;
                    2'b11: done_dec_logic = 0;
                endcase
            end
        end
    end
    
    always@(*)
    begin
        done_logic = 0;
        if(var) begin
            if(limit)
                done_logic = 0;
            else if(dual) begin
                case({inc,dec})
                    2'b00: done_logic = 0;
                    2'b01: done_logic = cnt_value <= zero_condition;
                    2'b10: done_logic = cnt_value >= var_condition;
                    2'b11: done_logic = 0;
                endcase
            end
            else if(en) begin
                if(cnt_en) begin
                    if(dir) begin
                        done_logic = cnt_value <= zero_condition;
                    end
                    else begin
                        done_logic = cnt_value >= var_condition;
                    end
                end
                else
                    done_logic = 0;
            end
            else begin
                if(dir) begin
                    done_logic = cnt_value <= zero_condition;
                end
                else begin
                    done_logic = cnt_value >= var_condition;
                end
            end
        end
        else begin
            if(limit)
                done_logic = 0;
            else if(dual) begin
                case({inc,dec})
                    2'b00: done_logic = 0;
                    2'b01: done_logic = cnt_value <= zero_condition;
                    2'b10: done_logic = cnt_value >= max_condition;
                    2'b11: done_logic = 0;
                endcase
            end
            else if(en) begin
                if(cnt_en) begin
                    if(dir) begin
                        done_logic = cnt_value <= zero_condition;
                    end
                    else begin
                        done_logic = cnt_value >= max_condition;
                    end
                end
                else
                    done_logic = 0;
            end
            else begin
                if(dir) begin
                    done_logic = cnt_value <= zero_condition;
                end
                else begin
                    done_logic = cnt_value >= max_condition;
                end
            end
        end
    end
    
    always@(*) begin
        if(var)
            full = cnt_value == var_value - 1;
        else
            full = cnt_value == max_value - 1;
    end
    
endmodule
