/*
 Crown Copyright 2012 AWE.

 This file is part of CloverLeaf.

 CloverLeaf is free software: you can redistribute it and/or modify it under
 the terms of the GNU General Public License as published by the
 Free Software Foundation, either version 3 of the License, or (at your option)
 any later version.

 CloverLeaf is distributed in the hope that it will be useful, but
 WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 details.

 You should have received a copy of the GNU General Public License along with
 CloverLeaf. If not, see http://www.gnu.org/licenses/.
 */

//  @brief Communication Utilities
//  @author Wayne Gaudin
//  @details Contains all utilities required to run CloverLeaf in a distributed
//  environment, including initialisation, mesh decompostion, reductions and
//  halo exchange using explicit buffers.
//
//  Note the halo exchange is currently coded as simply as possible and no
//  optimisations have been implemented, such as post receives before sends or packing
//  buffers with multiple data fields. This is intentional so the effect of these
//  optimisations can be measured on large systems, as and when they are added.
//
//  Even without these modifications CloverLeaf weak scales well on moderately sized
//  systems of the order of 10K cores.

#include "comms_kernel.h"
#include "comms.h"
#include "pack_kernel.h"

void clover_allocate_buffers(global_variables &globals, parallel_ &parallel) {

  // Unallocated buffers for external boundaries caused issues on some systems so they are now
  //  all allocated
  if (parallel.task == globals.chunk.task) {

    //		new(&globals.chunk.left_snd_buffer)   Kokkos::View<double *>("left_snd_buffer", 10 * 2 *
    //(globals.chunk.y_max +	5)); 		new(&globals.chunk.left_rcv_buffer)   Kokkos::View<double
    //*>("left_rcv_buffer", 10 * 2 * (globals.chunk.y_max +	5)); 		new(&globals.chunk.right_snd_buffer)
    // Kokkos::View<double *>("right_snd_buffer", 10
    //* 2 * (globals.chunk.y_max +	5)); 		new(&globals.chunk.right_rcv_buffer)  Kokkos::View<double
    //*>("right_rcv_buffer", 10 * 2 * (globals.chunk.y_max +	5)); 		new(&globals.chunk.bottom_snd_buffer)
    // Kokkos::View<double *>("bottom_snd_buffer", 10 * 2 * (globals.chunk.x_max +	5));
    //		new(&globals.chunk.bottom_rcv_buffer) Kokkos::View<double *>("bottom_rcv_buffer", 10 * 2 *
    //(globals.chunk.x_max +	5)); 		new(&globals.chunk.top_snd_buffer)    Kokkos::View<double
    //*>("top_snd_buffer", 10
    //* 2 * (globals.chunk.x_max +	5)); 		new(&globals.chunk.top_rcv_buffer)    Kokkos::View<double
    //*>("top_rcv_buffer", 10 * 2 * (globals.chunk.x_max +	5));
    //
    //		// Create host mirrors of device buffers. This makes this, and deep_copy, a no-op if the View is in host
    // memory already. 		globals.chunk.hm_left_snd_buffer = Kokkos::create_mirror_view(
    // globals.chunk.left_snd_buffer); 		globals.chunk.hm_left_rcv_buffer = Kokkos::create_mirror_view(
    //				globals.chunk.left_rcv_buffer);
    //		globals.chunk.hm_right_snd_buffer = Kokkos::create_mirror_view(
    //				globals.chunk.right_snd_buffer);
    //		globals.chunk.hm_right_rcv_buffer = Kokkos::create_mirror_view(
    //				globals.chunk.right_rcv_buffer);
    //		globals.chunk.hm_bottom_snd_buffer = Kokkos::create_mirror_view(
    //				globals.chunk.bottom_snd_buffer);
    //		globals.chunk.hm_bottom_rcv_buffer = Kokkos::create_mirror_view(
    //				globals.chunk.bottom_rcv_buffer);
    //		globals.chunk.hm_top_snd_buffer = Kokkos::create_mirror_view(globals.chunk.top_snd_buffer);
    //		globals.chunk.hm_top_rcv_buffer = Kokkos::create_mirror_view(globals.chunk.top_rcv_buffer);
  }
}

void clover_send_recv_message(global_variables &globals, chunk_neighbour_type tpe, clover::Buffer1D<double> &snd_buffer,
                              clover::Buffer1D<double> &rcv_buffer, int total_size, int tag_send, int tag_recv, MPI_Request &req_send,
                              MPI_Request &req_recv) {
  int task = globals.chunk.chunk_neighbours[tpe] - 1;
  MPI_Isend(snd_buffer.data, total_size, MPI_DOUBLE, task, tag_send, MPI_COMM_WORLD, &req_send);
  MPI_Irecv(rcv_buffer.data, total_size, MPI_DOUBLE, task, tag_recv, MPI_COMM_WORLD, &req_recv);
}

void clover_send_recv_message(global_variables &globals, chunk_neighbour_type tpe, double *snd_buffer, double *rcv_buffer, int total_size,
                              int tag_send, int tag_recv, MPI_Request &req_send, MPI_Request &req_recv) {
  int task = globals.chunk.chunk_neighbours[tpe] - 1;
  MPI_Isend(snd_buffer, total_size, MPI_DOUBLE, task, tag_send, MPI_COMM_WORLD, &req_send);
  MPI_Irecv(rcv_buffer, total_size, MPI_DOUBLE, task, tag_recv, MPI_COMM_WORLD, &req_recv);
}

void clover_exchange(global_variables &globals, const int fields[NUM_FIELDS], const int depth) {

  // Assuming 1 patch per task, this will be changed

  int left_right_offset[NUM_FIELDS];
  int bottom_top_offset[NUM_FIELDS];

  MPI_Request request[4] = {0};
  int message_count = 0;

  int end_pack_index_left_right = 0;
  int end_pack_index_bottom_top = 0;
  for (int field = 0; field < NUM_FIELDS; ++field) {
    if (fields[field] == 1) {
      left_right_offset[field] = end_pack_index_left_right;
      bottom_top_offset[field] = end_pack_index_bottom_top;
      end_pack_index_left_right += depth * (globals.chunk.y_max + 5);
      end_pack_index_bottom_top += depth * (globals.chunk.x_max + 5);
    }
  }

  static clover::Buffer1D<double> left_rcv_buffer(globals.context, end_pack_index_left_right);
  static clover::Buffer1D<double> left_snd_buffer(globals.context, end_pack_index_left_right);
  static clover::Buffer1D<double> right_rcv_buffer(globals.context, end_pack_index_left_right);
  static clover::Buffer1D<double> right_snd_buffer(globals.context, end_pack_index_left_right);

  static clover::Buffer1D<double> top_rcv_buffer(globals.context, end_pack_index_bottom_top);
  static clover::Buffer1D<double> top_snd_buffer(globals.context, end_pack_index_bottom_top);
  static clover::Buffer1D<double> bottom_rcv_buffer(globals.context, end_pack_index_bottom_top);
  static clover::Buffer1D<double> bottom_snd_buffer(globals.context, end_pack_index_bottom_top);

  double *h_left_rcv_buffer, *h_left_snd_buffer;
  double *h_right_rcv_buffer, *h_right_snd_buffer;
  double *h_top_rcv_buffer, *h_top_snd_buffer;
  double *h_bottom_rcv_buffer, *h_bottom_snd_buffer;

  if (globals.config.staging_buffer) {
    h_left_rcv_buffer = new double[left_rcv_buffer.size];
    h_left_snd_buffer = new double[left_snd_buffer.size];
    h_right_rcv_buffer = new double[right_rcv_buffer.size];
    h_right_snd_buffer = new double[right_snd_buffer.size];

    h_top_rcv_buffer = new double[top_rcv_buffer.size];
    h_top_snd_buffer = new double[top_snd_buffer.size];
    h_bottom_rcv_buffer = new double[bottom_rcv_buffer.size];
    h_bottom_snd_buffer = new double[bottom_snd_buffer.size];
  }

  if (globals.chunk.chunk_neighbours[chunk_left] != external_face) {
    // do left exchanges
    // Find left hand tiles
    for (int tile = 0; tile < globals.config.tiles_per_chunk; ++tile) {
      if (globals.chunk.tiles[tile].info.external_tile_mask[tile_left] == 1) {
        clover_pack_left(globals, left_snd_buffer, tile, fields, depth, left_right_offset);
      }
    }
    globals.context.queue.wait_and_throw();

    // send and recv messages to the left
    if (!globals.config.staging_buffer) {
      clover_send_recv_message(globals, chunk_left, left_snd_buffer, left_rcv_buffer, end_pack_index_left_right, 1, 2,
                               request[message_count], request[message_count + 1]);
    } else {
      auto ev1 = globals.context.queue.copy(left_rcv_buffer.data, h_left_rcv_buffer, left_rcv_buffer.size);
      auto ev2 = globals.context.queue.copy(left_snd_buffer.data, h_left_snd_buffer, left_snd_buffer.size);
      ev1.wait();
      ev2.wait();
      clover_send_recv_message(globals, chunk_left, h_left_snd_buffer, h_left_rcv_buffer, end_pack_index_left_right, 1, 2,
                               request[message_count], request[message_count + 1]);
    }

    message_count += 2;
  }

  if (globals.chunk.chunk_neighbours[chunk_right] != external_face) {
    // do right exchanges
    for (int tile = 0; tile < globals.config.tiles_per_chunk; ++tile) {
      if (globals.chunk.tiles[tile].info.external_tile_mask[tile_right] == 1) {
        clover_pack_right(globals, right_snd_buffer, tile, fields, depth, left_right_offset);
      }
    }
    globals.context.queue.wait_and_throw();

    // send message to the right
    if (!globals.config.staging_buffer) {
      clover_send_recv_message(globals, chunk_right, right_snd_buffer, right_rcv_buffer, end_pack_index_left_right, 2, 1,
                               request[message_count], request[message_count + 1]);
    } else {
      auto ev1 = globals.context.queue.copy(right_rcv_buffer.data, h_right_rcv_buffer, right_rcv_buffer.size);
      auto ev2 = globals.context.queue.copy(right_snd_buffer.data, h_right_snd_buffer, right_snd_buffer.size);
      ev1.wait();
      ev2.wait();
      clover_send_recv_message(globals, chunk_right, h_right_snd_buffer, h_right_rcv_buffer, end_pack_index_left_right, 2, 1,
                               request[message_count], request[message_count + 1]);
    }

    message_count += 2;
  }

  // make a call to wait / sync
  MPI_Waitall(message_count, request, MPI_STATUS_IGNORE);

  // Copy back to the device
  if (globals.config.staging_buffer) {
    if (globals.chunk.chunk_neighbours[chunk_left] != external_face)
      globals.context.queue.copy(h_left_rcv_buffer, left_rcv_buffer.data, left_rcv_buffer.size);
    if (globals.chunk.chunk_neighbours[chunk_right] != external_face)
      globals.context.queue.copy(h_right_rcv_buffer, right_rcv_buffer.data, right_rcv_buffer.size);
  }

  globals.context.queue.wait_and_throw();

  // unpack in left direction
  if (globals.chunk.chunk_neighbours[chunk_left] != external_face) {
    for (int tile = 0; tile < globals.config.tiles_per_chunk; ++tile) {
      if (globals.chunk.tiles[tile].info.external_tile_mask[tile_left] == 1) {
        clover_unpack_left(globals, left_rcv_buffer, fields, tile, depth, left_right_offset);
      }
    }
  }

  // unpack in right direction
  if (globals.chunk.chunk_neighbours[chunk_right] != external_face) {
    for (int tile = 0; tile < globals.config.tiles_per_chunk; ++tile) {
      if (globals.chunk.tiles[tile].info.external_tile_mask[tile_right] == 1) {
        clover_unpack_right(globals, right_rcv_buffer, fields, tile, depth, left_right_offset);
      }
    }
  }

  globals.context.queue.wait_and_throw();

  message_count = 0;
  for (MPI_Request &i : request)
    i = {};

  if (globals.chunk.chunk_neighbours[chunk_bottom] != external_face) {
    // do bottom exchanges
    for (int tile = 0; tile < globals.config.tiles_per_chunk; ++tile) {
      if (globals.chunk.tiles[tile].info.external_tile_mask[tile_bottom] == 1) {
        clover_pack_bottom(globals, bottom_snd_buffer, tile, fields, depth, bottom_top_offset);
      }
    }
    globals.context.queue.wait_and_throw();

    // send message downwards
    if (!globals.config.staging_buffer) {
      clover_send_recv_message(globals, chunk_bottom, bottom_snd_buffer, bottom_rcv_buffer, end_pack_index_bottom_top, 3, 4,
                               request[message_count], request[message_count + 1]);
    } else {
      auto ev1 = globals.context.queue.copy(bottom_rcv_buffer.data, h_bottom_rcv_buffer, bottom_rcv_buffer.size);
      auto ev2 = globals.context.queue.copy(bottom_snd_buffer.data, h_bottom_snd_buffer, bottom_snd_buffer.size);
      ev1.wait();
      ev2.wait();
      clover_send_recv_message(globals, chunk_bottom, h_bottom_snd_buffer, h_bottom_rcv_buffer, end_pack_index_bottom_top, 3, 4,
                               request[message_count], request[message_count + 1]);
    }

    message_count += 2;
  }

  if (globals.chunk.chunk_neighbours[chunk_top] != external_face) {
    // do top exchanges
    for (int tile = 0; tile < globals.config.tiles_per_chunk; ++tile) {
      if (globals.chunk.tiles[tile].info.external_tile_mask[tile_top] == 1) {
        clover_pack_top(globals, top_snd_buffer, tile, fields, depth, bottom_top_offset);
      }
    }
    globals.context.queue.wait_and_throw();

    // send message upwards
    if (!globals.config.staging_buffer) {
      clover_send_recv_message(globals, chunk_top, top_snd_buffer, top_rcv_buffer, end_pack_index_bottom_top, 4, 3, request[message_count],
                               request[message_count + 1]);
    } else {
      auto ev1 = globals.context.queue.copy(top_rcv_buffer.data, h_top_rcv_buffer, top_rcv_buffer.size);
      auto ev2 = globals.context.queue.copy(top_snd_buffer.data, h_top_snd_buffer, top_snd_buffer.size);
      ev1.wait();
      ev2.wait();
      clover_send_recv_message(globals, chunk_top, h_top_snd_buffer, h_top_rcv_buffer, end_pack_index_bottom_top, 4, 3,
                               request[message_count], request[message_count + 1]);
    }

    message_count += 2;
  }

  // need to make a call to wait / sync
  MPI_Waitall(message_count, request, MPI_STATUS_IGNORE);

  // Copy back to the device
  if (globals.config.staging_buffer) {
    if (globals.chunk.chunk_neighbours[chunk_bottom] != external_face)
      globals.context.queue.copy(h_bottom_rcv_buffer, bottom_rcv_buffer.data, bottom_rcv_buffer.size);
    if (globals.chunk.chunk_neighbours[chunk_top] != external_face)
      globals.context.queue.copy(h_top_rcv_buffer, top_rcv_buffer.data, top_rcv_buffer.size);
  }

  globals.context.queue.wait_and_throw();

  // unpack in top direction
  if (globals.chunk.chunk_neighbours[chunk_top] != external_face) {
    for (int tile = 0; tile < globals.config.tiles_per_chunk; ++tile) {
      if (globals.chunk.tiles[tile].info.external_tile_mask[tile_top] == 1) {
        clover_unpack_top(globals, top_rcv_buffer, fields, tile, depth, bottom_top_offset);
      }
    }
  }

  // unpack in bottom direction
  if (globals.chunk.chunk_neighbours[chunk_bottom] != external_face) {
    for (int tile = 0; tile < globals.config.tiles_per_chunk; ++tile) {
      if (globals.chunk.tiles[tile].info.external_tile_mask[tile_bottom] == 1) {
        clover_unpack_bottom(globals, bottom_rcv_buffer, fields, tile, depth, bottom_top_offset);
      }
    }
  }

  globals.context.queue.wait_and_throw();

  if (globals.config.staging_buffer) {
    delete[] h_left_rcv_buffer;
    delete[] h_left_snd_buffer;
    delete[] h_right_rcv_buffer;
    delete[] h_right_snd_buffer;

    delete[] h_top_rcv_buffer;
    delete[] h_top_snd_buffer;
    delete[] h_bottom_rcv_buffer;
    delete[] h_bottom_snd_buffer;
  }
}
