-- derived from sqlprocbench, udf in both predicate and projection, bigdifference using result cache;
select c_customer_sk, sudf_12_increaseInWebSpending(c_customer_sk) 
from customer
where c_customer_sk in 
	(select ws_bill_customer_sk
	from web_sales_history, date_dim
	where d_date_sk = ws_sold_date_sk
		and d_year = 2000

	INTERSECT

	select ws_bill_customer_sk
	from web_sales_history, date_dim
	where d_date_sk = ws_sold_date_sk
		and d_year = 2001
	)
	and sudf_12_increaseInWebSpending(c_customer_sk) > 0 limit 10; 

-- derived from q45, udf in projection, no significant difference using result cache;
select c1, c2, sudf_12_increaseInWebSpending(c_customer_sk) from (SELECT ca_zip, 
               ca_state c1, 
               Sum(ws_sales_price) c2, c_customer_sk
FROM   web_sales, 
       customer, 
       customer_address, 
       date_dim, 
       item 
WHERE  ws_bill_customer_sk = c_customer_sk 
       AND c_current_addr_sk = ca_address_sk 
       AND ws_item_sk = i_item_sk 
       AND ( Substr(ca_zip, 1, 5) IN ( '85669', '86197', '88274', '83405', 
                                       '86475', '85392', '85460', '80348', 
                                       '81792' ) 
              OR i_item_id IN (SELECT i_item_id 
                               FROM   item 
                               WHERE  i_item_sk IN ( 2, 3, 5, 7, 
                                                     11, 13, 17, 19, 
                                                     23, 29 )) ) 
       AND ws_sold_date_sk = d_date_sk 
       AND d_qoy = 1 
       AND d_year = 2000 
GROUP  BY ca_zip, 
          ca_state, c_customer_sk 
ORDER  BY ca_zip, 
          ca_state) t; 

-- derived from q45, udf in projection and predicate, no difference using resultcache;
select c1, c2, sudf_12_increaseInWebSpending(c_customer_sk) from (SELECT ca_zip, 
               ca_state c1, 
               Sum(ws_sales_price) c2, c_customer_sk
FROM   web_sales, 
       customer, 
       customer_address, 
       date_dim, 
       item 
WHERE  ws_bill_customer_sk = c_customer_sk 
       AND c_current_addr_sk = ca_address_sk 
       AND ws_item_sk = i_item_sk 
       AND ( Substr(ca_zip, 1, 5) IN ( '85669', '86197', '88274', '83405', 
                                       '86475', '85392', '85460', '80348', 
                                       '81792' ) 
              OR i_item_id IN (SELECT i_item_id 
                               FROM   item 
                               WHERE  i_item_sk IN ( 2, 3, 5, 7, 
                                                     11, 13, 17, 19, 
                                                     23, 29 )) ) 
       AND ws_sold_date_sk = d_date_sk 
       AND d_qoy = 1 
       AND d_year = 2000 
GROUP  BY ca_zip, 
          ca_state, c_customer_sk 
ORDER  BY ca_zip, 
          ca_state) t where sudf_12_increaseInWebSpending(c_customer_sk) > 0; 