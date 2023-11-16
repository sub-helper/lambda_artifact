package org.example.pgsql_udf;

import static org.postgresql.pljava.annotation.Function.Effects;
import static org.postgresql.pljava.annotation.Function.OnNullInput;
import static org.postgresql.pljava.annotation.Function.Parallel;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.PrintStream;
import java.lang.annotation.RetentionPolicy;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Random;
import java.util.Set;
import org.postgresql.pljava.annotation.Function;
import org.postgresql.pljava.annotation.Function.Effects;

public class sudf_15 {

  private static String m_url = "jdbc:default:connection";
  private static String remove_trailingspace_regex = "\\s+$";
  private static HashMap<Integer, String> cache = new HashMap<Integer, String>();

  public static String evaluation(Integer storeNumber) throws SQLException {
    Integer cust = null;
    Integer hhdemo = null;
    Integer cntVar = null;
    Integer incomeband = null;
    String cLevel = "";

    String query1 =
      "select ss_customer_sk , c_current_hdemo_sk , count(*) from store_sales_history, customer where ss_store_sk = 1 and c_customer_sk=ss_customer_sk group by ss_customer_sk, c_current_hdemo_sk having count(*) =(select max(cnt) from (select ss_customer_sk, c_current_hdemo_sk, count(*) as cnt from store_sales_history, customer where ss_store_sk = ? and c_customer_sk=ss_customer_sk group by ss_customer_sk, c_current_hdemo_sk having ss_customer_sk IS NOT NULL ) tbl)";
    Connection conn = DriverManager.getConnection(m_url);
    PreparedStatement stmt1 = conn.prepareStatement(query1);
    stmt1.setInt(1, storeNumber);
    ResultSet rs1 = stmt1.executeQuery();
    if (rs1.next()) {
      cust = rs1.getInt(1);
      if (cust == null) {
        cust = Integer.valueOf(0);
      }
      hhdemo = rs1.getInt(2);
      if (hhdemo == null) {
        hhdemo = Integer.valueOf(0);
      }
      cntVar = rs1.getInt(3);
      if (cntVar == null) {
        cntVar = Integer.valueOf(0);
      }
    } else {
      hhdemo = 0;
    }
    String query2 =
      "select hd_income_band_sk from household_demographics where hd_demo_sk = ?";
    PreparedStatement stmt2 = conn.prepareStatement(query2);
    stmt2.setInt(1, hhdemo);
    ResultSet rs2 = stmt2.executeQuery();
    if (rs2.next()) {
      incomeband = rs2.getInt(1);
      if (incomeband == null) {
        incomeband = Integer.valueOf(0);
      }
    } else {
      incomeband = 0;
    }
    if (incomeband >= 0 && incomeband <= 3) {
      return "low";
    }
    if (incomeband >= 4 && incomeband <= 7) {
      return "lowerMiddle";
    }
    if (incomeband >= 8 && incomeband <= 11) {
      return "upperMiddle";
    }
    if (incomeband >= 12 && incomeband <= 16) {
      return "high";
    }
    if (incomeband >= 17 && incomeband <= 20) {
      return "affluent";
    }
    stmt1.close();
    stmt2.close();
    conn.close();
    return cLevel;
  }

  @Function
  public static String sudf_15_incomeBandOfMaxBuy_cache(Integer storeNumber)
    throws SQLException {
    if (storeNumber == null) {
      return null;
    }
    if (cache.containsKey(storeNumber)) {
      return cache.get(storeNumber);
    } else {
      String result = evaluation(storeNumber);
      cache.put(storeNumber, result);
      return result;
    }
  }

  @Function
  public static String sudf_15_incomeBandOfMaxBuy(Integer storeNumber)
    throws SQLException {
    if (storeNumber == null) {
      return null;
    }
    return evaluation(storeNumber);
  }
  @Function(onNullInput = OnNullInput.RETURNS_NULL)
  public static String sudf_15_incomeBandOfMaxBuy_null(Integer storeNumber)
    throws SQLException {
    if (storeNumber == null) {
      return null;
    }
    return evaluation(storeNumber);
  }
  @Function(parallel = Parallel.SAFE)
  public static String sudf_15_incomeBandOfMaxBuy_safe(Integer storeNumber)
    throws SQLException {
    if (storeNumber == null) {
      return null;
    }
    return evaluation(storeNumber);
  }
}
