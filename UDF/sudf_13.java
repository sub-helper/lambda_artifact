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

public class sudf_13 {

  private static String m_url = "jdbc:default:connection";
  private static String remove_trailingspace_regex = "\\s+$";
  private static HashMap<String, String> cache = new HashMap<String, String>();

  public static String evaluation(
    Integer ckey,
    Integer fromDateSk,
    Integer toDateSk
  ) throws SQLException {
    Integer result1 = null;
    Integer result2 = null;
    Integer result3 = null;

    String maxChannel = "Web";
    String query1 =
      "select count(*) numSalesFromStore from store_sales_history where ss_customer_sk = ? and ss_sold_date_sk>=? and ss_sold_date_sk<=?";
    String query2 =
      "select count(*) numSalesFromCatalog from catalog_sales_history where cs_bill_customer_sk = ? and cs_sold_date_sk>=? and cs_sold_date_sk<=?";
    String query3 =
      "select count(*) from web_sales_history where ws_bill_customer_sk = ? and ws_sold_date_sk>=? and ws_sold_date_sk<=?";

    Connection conn = DriverManager.getConnection(m_url);
    PreparedStatement stmt1 = conn.prepareStatement(query1);
    stmt1.setInt(1, ckey);
    stmt1.setInt(2, fromDateSk);
    stmt1.setInt(3, toDateSk);
    ResultSet rs1 = stmt1.executeQuery();
    if (rs1.next()) {
      result1 = rs1.getInt(1);
      if (result1 == null) {
        result1 = Integer.valueOf(0);
      }
    }
    PreparedStatement stmt2 = conn.prepareStatement(query2);
    stmt2.setInt(1, ckey);
    stmt2.setInt(2, fromDateSk);
    stmt2.setInt(3, toDateSk);
    ResultSet rs2 = stmt2.executeQuery();
    if (rs2.next()) {
      result2 = rs2.getInt(1);
      if (result2 == null) {
        result2 = Integer.valueOf(0);
      }
    }
    PreparedStatement stmt3 = conn.prepareStatement(query3);
    stmt3.setInt(1, ckey);
    stmt3.setInt(2, fromDateSk);
    stmt3.setInt(3, toDateSk);
    ResultSet rs3 = stmt3.executeQuery();
    if (rs3.next()) {
      result3 = rs3.getInt(1);
      if (result3 == null) {
        result3 = Integer.valueOf(0);
      }
    }
    if (result1 > result2) {
      maxChannel = "Store";
      if (result3 > result1) {
        maxChannel = "Web";
      }
    } else {
      maxChannel = "Catalog";
      if (result3 > result2) {
        maxChannel = "Web";
      }
    }

    stmt1.close();
    stmt2.close();
    stmt3.close();

    conn.close();
    return maxChannel;
  }

  @Function
  public static String sudf_13_maxPurchaseChannel(
    Integer ckey,
    Integer fromDateSk,
    Integer toDateSk
  ) throws SQLException {
    if (ckey == null || fromDateSk == null || toDateSk == null) {
      return null;
    }
    return evaluation(ckey, fromDateSk, toDateSk);
  }
  @Function(onNullInput = OnNullInput.RETURNS_NULL)
  public static String sudf_13_maxPurchaseChannel_null(
    Integer ckey,
    Integer fromDateSk,
    Integer toDateSk
  ) throws SQLException {
    if (ckey == null || fromDateSk == null || toDateSk == null) {
      return null;
    }
    return evaluation(ckey, fromDateSk, toDateSk);
  }
  @Function(parallel = Parallel.SAFE)
  public static String sudf_13_maxPurchaseChannel_safe(
    Integer ckey,
    Integer fromDateSk,
    Integer toDateSk
  ) throws SQLException {
    if (ckey == null || fromDateSk == null || toDateSk == null) {
      return null;
    }
    return evaluation(ckey, fromDateSk, toDateSk);
  }

  @Function
  public static String sudf_13_maxPurchaseChannel_cache(
    Integer ckey,
    Integer fromDateSk,
    Integer toDateSk
  ) throws SQLException {
        if (ckey == null || fromDateSk == null || toDateSk == null) {
      return null;
    }
    String key = ckey.toString() + fromDateSk.toString() + toDateSk.toString();
    if (cache.containsKey(key)) {
      return cache.get(key);
    } else {
      String result = evaluation(ckey, fromDateSk, toDateSk);
      cache.put(key, result);
      return result;
    }
  }
}
