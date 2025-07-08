`timescale 1ns / 1ps


//整合版本的计数器模块，可以通过参数配置各项功能
//dir:0为加法计数器，1为减法计数器
//en:为1则带en使能端
//limit:为1则到达限制值后不归零至初始值，limit型计数器done永远不会为高，请改用empty和full信号
//var:为1则上限值为非固定而是可配置，此时var_value可以超过max_value，但不能超出位宽
//比如max_value为10，对应计数器位宽为4bits,那么var_value最大值只能设置为16，超出16结果将不可预期。
//dual:为1则该计数器为双向计数器,此时dir参数不再生效，dual型计数器上/下溢出都会使done信号置1
//done_reg:设置计数器的done信号输出为纯组合逻辑还是寄存器缓存(会自动矫正计数，开启并不会因为寄存器引入计数误差)
//为0：纯组合逻辑无缝衔接响应更快，但是组合逻辑done串接下一层计数器或其他组合逻辑会增大逻辑级数
//为1：时序逻辑会有一个时钟周期延迟，但是通过寄存器切割了逻辑级数，适合高频场合
//max_value:上限值，如果var为1，则该数值为最大可设置上限值
//max_value:上限值，如果var为0，则该数值为每次溢出上限值（当dir为0）或重载值（当dir为1）

//端口功能
//cnt_value:计数器当前数值
//done:计数器是否上/下溢
//inc_done、dec_done:仅在dual模式下生效，用于区分双向计数器是上溢还是下溢出

//注意：开了limit模式，任何done信号都不会产生。
//在非limit情况下，任何上下溢都会产生一个done信号，
//在dual模式下会额外产生inc_done或dec_done作为区分

//empty:cnt_value是否为0
//full:cnt_value是否为max_value(var为0)，cnt_value是否为var_value(var为1)
//cnt_en:如果参数en为1，则由该端口使能计数器
//inc,dec:如果为双向计数器，cnt_en端失效，此时inc为加法使能，dec为减法使能
//clk:时钟信号
//rst:复位信号
//由于var为system_verilog原语言，如使用system_verilog实例化本模块时
//可以用var_option参数代替var使用

//新增参数，load:载入数值端口，之前版本仅用于减法计数器重载上限值时使用
//新版本设置为全局可用，当时钟上升沿到来，非复位情况下load端口为高电平时
//将强制设置计数器为load_value设置值
module cnt
    #(parameter
    dir = 0,            //方向控制
    en = 0,             //是否打开使能功能，与双向互斥
    limit = 0,          //是否打开限制溢出
    var_option = 0,     //是否打开上限值可变化
    var = var_option,   //
    dual = 0,           //是否打开双向计数功能（打开dual为1时en要设置为0）
    done_reg = 0,       //done输出是否为寄存器形式（除非多个计数器级联导致时序分析通过不了，否则不要打开）
    max_value = 10,     //最大上限值
    width = max_value > 1 ? clog2(max_value) : 1    //自动计算，无需配置
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
    input clk,                          //模块时钟
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
    
    always@(posedge clk or negedge rst)
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
            else begin  //此处spi_slave调用
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
                        if(dir) begin//此处spi_slave调用
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
    
    always@(posedge clk or negedge rst)
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
    
    always@(posedge clk or negedge rst)
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
    
    always@(posedge clk or negedge rst)
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
